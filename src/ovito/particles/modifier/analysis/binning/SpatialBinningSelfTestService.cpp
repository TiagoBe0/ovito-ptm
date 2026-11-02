////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//  If you do not alter this notice, a recipient may use your version of this
//  file under either the GPL or the MIT License.
//
//  You should have received a copy of the GPL along with this program in a
//  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
//  with this program in a file LICENSE.MIT.txt
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
//  either express or implied. See the GPL or the MIT License for the specific language
//  governing rights and limitations.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/Particles.h>
#include <ovito/particles/modifier/analysis/binning/SpatialBinningModifier.h>
#include <ovito/stdmod/modifiers/ExpressionSelectionModifier.h>
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "SpatialBinningSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(SpatialBinningSelfTestService);

namespace {

/// Writes 8 atoms in a 10x10x10 box. Frame 0 is fully periodic, frame 1 is non-periodic along x.
/// Atom 5 sits at x = -0.5: it wraps into the last x bin on frame 0 and is discarded on frame 1.
/// Frame 2 shifts the cell origin to (-5,-5,-5); frame 3 is triclinic with tilt xy = 4 (|b| = sqrt(116)).
///
///   id   x    y    z    vx
///    1  0.5  0.5  0.5   1
///    2  1.5  0.5  0.5   3
///    3  2.5  0.5  0.5   5
///    4  9.5  0.5  0.5   7
///    5 -0.5  0.5  0.5   9
///    6  4.9  7.0  3.0   2
///    7  5.1  2.0  8.0   4
///    8  7.5  9.0  1.0   6
void writeDump(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    const char* atoms =
        "1 1 0.5 0.5 0.5 1 0 0\n"
        "2 1 1.5 0.5 0.5 3 0 0\n"
        "3 1 2.5 0.5 0.5 5 0 0\n"
        "4 1 9.5 0.5 0.5 7 0 0\n"
        "5 1 -0.5 0.5 0.5 9 0 0\n"
        "6 1 4.9 7.0 3.0 2 0 0\n"
        "7 1 5.1 2.0 8.0 4 0 0\n"
        "8 1 7.5 9.0 1.0 6 0 0\n";
    for(int frame = 0; frame < 4; frame++) {
        out << "ITEM: TIMESTEP\n" << frame << "\n";
        out << "ITEM: NUMBER OF ATOMS\n8\n";
        if(frame < 2)
            out << "ITEM: BOX BOUNDS " << (frame == 0 ? "pp" : "ff") << " pp pp\n0 10\n0 10\n0 10\n";
        else if(frame == 2)
            out << "ITEM: BOX BOUNDS pp pp pp\n-5 5\n-5 5\n-5 5\n";
        else
            out << "ITEM: BOX BOUNDS xy xz yz pp pp pp\n0 14 4\n0 10 0\n0 10 0\n";
        out << "ITEM: ATOMS id type x y z vx vy vz\n" << atoms;
    }
}

std::vector<double> column(const Property* property, size_t component = 0)
{
    std::vector<double> values;
    if(!property)
        return values;
    values.resize(property->size());
    property->copyComponentTo(values.begin(), component);
    return values;
}

bool check(const QString& name, const std::vector<double>& actual, const std::vector<double>& expected)
{
    bool ok = actual.size() == expected.size();
    for(size_t i = 0; ok && i < actual.size(); i++)
        ok = std::abs(actual[i] - expected[i]) < 1e-9;
    QStringList a, e;
    for(double v : actual) a << QString::number(v);
    for(double v : expected) e << QString::number(v);
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << a.join(' ');
    else
        qCritical().noquote() << "  FAIL" << name << ": got [" << a.join(' ') << "] expected [" << e.join(' ') << "]";
    return ok;
}


/// One dislocation line of a test CA file: Burgers vector in lattice coordinates and polyline vertices.
struct CALine {
    Vector3 burgers;
    std::vector<Point3> points;
};

/// The three lines of the dislocation test case: one crossing the periodic x boundary twice, a 3D diagonal and a closed loop.
const std::vector<CALine> testLines = {
    {{0.5, 0.0, 0.5}, {{-6, 10, 4}, {3, 10.5, 6}, {12, 11, 9}, {47, 12, 15}}},
    {{0.0, 0.5, -0.5}, {{5, 3, 1}, {30, 25, 18}}},
    {{0.5, 0.5, 0.0}, {{20, 15, 10}, {26, 15, 10}, {26, 21, 12}, {20, 21, 10}, {20, 15, 10}}}};

/// Writes a crystal analysis (CA) file with a 40 x 30 x 20 cell (optional xy tilt), one fcc cluster whose orientation
/// is a general (rotated and scaled) matrix, and the given dislocation lines. The same file, written by
/// research/pro_ports/scripts/spatial_binning/mk_ca.py, was the input of the OVITO Pro reference run.
void writeCA(const QString& path, const std::vector<CALine>& lines, bool pbcX = true, double tiltXY = 0)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test CA file %1").arg(path));
    QTextStream out(&file);
    out << "CA_FILE_VERSION 6\nCA_LIB_VERSION 0.0.0\nSTRUCTURE_TYPES 1\nSTRUCTURE_TYPE 1\nNAME fcc\nFULL_NAME FCC\nCOLOR 0.4 1.0 0.4\nTYPE LATTICE\n"
           "BURGERS_VECTOR_FAMILIES 1\nBURGERS_VECTOR_FAMILY ID 0\nOther\n0 0 0\n0.9 0.9 0.9\nEND_STRUCTURE_TYPE\n"
           "SIMULATION_CELL_ORIGIN 0.0 0.0 0.0\nSIMULATION_CELL_MATRIX\n40 " << tiltXY << " 0\n0 30 0\n0 0 20\n"
        << "PBC_FLAGS " << (pbcX ? 1 : 0) << " 1 1\n"
           "CLUSTERS 1\nCLUSTER 1\nCLUSTER_STRUCTURE 1\nCLUSTER_ORIENTATION\n2.8 -2.9 0.1\n2.4 2.3 -2.3\n1.6 1.7 3.3\n"
           "CLUSTER_COLOR 1 1 1\nCLUSTER_SIZE 100\nEND_CLUSTER\nCLUSTER_TRANSITIONS 0\n"
        << "DISLOCATIONS " << lines.size() << "\n";
    for(size_t i = 0; i < lines.size(); i++) {
        out << i << "\n" << lines[i].burgers.x() << " " << lines[i].burgers.y() << " " << lines[i].burgers.z() << "\n1\n" << lines[i].points.size() << "\n";
        for(const Point3& p : lines[i].points)
            out << p.x() << " " << p.y() << " " << p.z() << "\n";
    }
    out << "DISLOCATION_JUNCTIONS\n";
    for(size_t i = 0; i < lines.size(); i++) out << "0 " << i << "\n1 " << i << "\n";   // Both ends of every line are free.
    out << "DEFECT_MESH_VERTICES 0\nDEFECT_MESH_FACETS 0\n";
}

// Dislocation density and Nye tensor (9 components per bin, XX..ZZ) computed by OVITO Pro 3.16.1
// (SpatialBinningModifier, operate_on='dislocations') for the file written by writeCA(testLines).
const std::vector<double> proX5Density = {
    0.0040558069060102949, 0.004317580486049675, 0.0073684691019370993, 0.0054877458079457672, 0.0029988728700001258,};
const std::vector<double> proX5Nye = {
    0.0034042966598644866, -0.00065236649415184686, 0.00038535254478907951, 0.0016178387263789764, 0.0012978997015064113, 0.0010335933670628237,
    0.0068587241321802122, -0.00012843457346673633, 0.0014036256334650768, 0.00010611966407547642, -0.0020763041553861147, -0.001047668682316701,
    0.0038925781737392104, 0.0033506518042503404, 0.0026083382715544972, 0.0030475262055794402, -0.00098420420951510408, 0.00018019104685693566,
    -8.333345254261949e-05, -0.0020840773173031336, -0.0012779017949742907, 0.0039166667064030915, 0.0011725893085911153, 0.0022537648622478735,
    0.0027500001589457227, -0.0026035416838668612, -0.00046447911390236249, 0.00069531237706541927, -0.0015548818497686952, -0.00079861941074646991,
    0.0028411458991467998, 0.0046302338255796039, 0.0022555935521762862, 0.0032526043057441727, 0.00082729759578588325, 0.00033450238273415126,
    0.0040755206284423624, 9.6796453474137743e-05, 0.00074925384600227796, 0.00026302103574077444, 0.00011112517132448592, 0.00010912660865812973,
    0.0069765626390774988, 0.00024304977614807011, 0.00132741032900121,};
const std::vector<double> proXY43Density = {
    0.0064438358509251959, 0.0046953102778814265, 0.0019069947311009456, 0.0031607100685962083, 0.0058102346758410273, 0.0096518074990576946,
    0.012951347803252772, 0.0053632505528396265, 0.0001570785213286821, 0.0011407983322371076, 0.0059586425000740604, 0.00090832959952839268,};
const std::vector<double> proXY43Nye = {
    0.0016360119752462337, -0.002497454108526807, -0.00091392374058081214, 0.0048929974209804723, 0.0041700739210460656, 0.0032573618710076647,
    0.0063314540526733746, -0.0010807377081834323, 0.00088145119946674976, 0.00023517235883362665, -0.0022362818155525995, -0.0011925256040593985,
    0.0041748074694461321, 0.0035895810177015721, 0.0027922330513107971, 0.003470453137221836, -0.0010742236292620893, 7.4745990069897882e-05,
    0.0025958794634426013, 2.9621022024104809e-05, 0.00041902548732641941, 0.00017243902825130321, 7.3996129603289459e-05, 7.0838727489027598e-05,
    0.0044473022964700258, 0.00010387084900871642, 0.00074959799675416495, 0.0044912309292502797, 0.00018411487344506918, 0.00088213961173064443,
    0.00015487026122590846, 6.3487981346736837e-06, 3.0418652315714217e-05, 0.0075886320921400227, 0.00031109066963492371, 0.0014905118602851649,
    0.0069515287462529963, -0.00012907820501263263, 0.0014222620116374322, 0.0010354397047516199, 0.0006957927075807468, 0.00059014098452920356,
    0.012332568500638514, 0.00029835777576352532, 0.002802211739245226, -6.2538452915476909e-05, -0.0037911836704727134, -0.0021464688586714945,
    0.0080069867077097196, 0.003671936173645968, 0.0045765344691377852, 0.0058013689960690383, -0.0036011871194179994, -0.0001968404192520866,
    0.0012352626382738062, -0.0028409188918456663, -0.0014854792644530572, 0.0085820424940615328, 0.0064741796438737651, 0.0042454801735260072,
    0.0083853242182603573, 4.7023019529318917e-05, 0.00065902896133640456, 0.0069342684992603877, 0.00013118072441566781, 0.0012386063058583011,
    0.00048996363672235848, 0.00068809127939628621, 0.00034608303504447499, 0.011901534960605803, 0.0007258065911539633, 0.002316565999748537,
    -0.0001575413223140492, -0.00013863636363636321, -0.00010712809917355351, 0.00024156335587343836, 0.00021257575316862564, 0.00016426308199393814,
    -8.4022033559389144e-05, -7.3939389532262428e-05, -5.7134982820384652e-05, -0.00027763427069857143, -0.00023831814963167422, -0.00019529131337335322,
    -0.00042679405079400266, -0.00065757875325463014, 1.5280033062312429e-05, -0.00077682165168533733, -0.00088160306492718842, -0.00031373871074826296,
    -0.0031073924611324105, -0.0028145454802946598, -0.0021509669278436446, 0.0015592686284869154, 0.0051340362404042944, 0.0028434825950062938,
    -0.0040213760665034444, -0.00089749078585885111, -0.0014825156528575363, -0.00079924999892711445, -0.00070850000643729952, -0.00054525000178813805,
    0.0011914166426658594, 0.0012909666357040332, 0.00089288331365584947, -0.00045141664266586253, -0.00022696663570404265, -0.00024888331365585388,};
const std::vector<double> proXYZ543Density = {
    0.0085949193186599365, 0.0079666105997857287, 6.9037633956089416e-05, 9.1059107101408224e-05, 0.0016354158860666099, 0.011903262797174092,
    0.013189722500693795, 0.0018960310251652168, 0.00047176058413969055, 0.0098863895518629206, 0, 0.00019814917646148621,
    0.00023087829416557182, 0.00015068177544054555, 2.7730156979065001e-05, 9.0736086226198863e-05, 1.7446514752924857e-05, 0,
    0.00045906536154410789, 9.565678822607643e-05, 0.0010035977060309452, 0.00193035873366838, 0.00018722046507474902, 1.2718443056417735e-05,
    0.00043345778771383263, 0.014438083832877893, 0.025373717733136663, 0.034891113059133304, 0.019364622277268637, 0.013006362937265006,
    0.00048018254806938735, 0.0014420142139084954, 0.036720122329428187, 0.01766231980048882, 0.00060719878853617774, 0,
    0, 0.0039373730619315858, 0.0026078620061784612, 2.879670764777825e-10, 0.001522842623139768, 0.00054338148528526348,
    9.2579347908752778e-07, 5.5527477513250821e-09, 0, 0.0096968914045912763, 0.0010176277085650678, 0.0020892167792791378,
    0.0053278514695135571, 0.0089107809620092033, 0.00089614549267092532, 0.00012380913316105746, 0.0070923339020430663, 0.011181259432055063,
    0.00077330131303910343, 4.3021062683135314e-05, 8.1280331772873308e-06, 0.0013073768795892305, 0.0085237438858147656, 0.00061017998033644242,};
const std::vector<double> proXYZ543Nye = {
    -0.005979219132336443, -0.0065289284682484303, -0.0046975979940303385, 0.011577953839112047, 0.010144902223002461, 0.0078512248202893247,
    -0.0014115873439862958, -0.003383354456821289, -0.0020272836127877138, -0.0075243111429051076, -0.0068446596394363124, -0.0052153123561248325,
    0.011960183048774891, 0.010517262249677854, 0.0081295182343693734, -0.003701056841057873, -0.0036341723167820773, -0.0026836241174877486,
    -5.4661199823652529e-05, -5.5395210037513736e-05, -4.152604892136061e-05, 9.7243782195944548e-05, 8.5323032988096937e-05, 6.5975549841928557e-05,
    -1.9247560871470009e-05, -2.9261108050559727e-05, -2.0449211520427025e-05, 0.00012870241134684278, 7.1501339637134618e-06, 2.8600535854853959e-05,
    4.4380207519227121e-06, 2.4655670844014974e-07, 9.8622683376060299e-07, 0.0002174627099937373, 1.2081261666318697e-05, 4.8325046665274979e-05,
    0.0023111145336311219, 0.00012839525186839564, 0.00051501372773898597, 7.9693722541716058e-05, 4.4274290300953359e-06, 1.7759120340574434e-05,
    0.0039049868944173827, 0.00021694371635652121, 0.00087019566879967475, 0.012114678278209912, -0.000865820459536088, 0.0026539991000797112,
    0.0033441388843887133, 0.0025453679285350884, 0.0020814630485394793, 0.022627950915490989, 0.00043638409089486824, 0.0059520016863879562,
    -0.0045432090058882303, -0.0081839620117361601, -0.0050253464703792061, 0.014866569086868319, 0.01293823789020029, 0.010042509489025917,
    0.0034037282405951297, -0.0040775115741695182, -0.0009565771928790528, -0.0012601043500765313, -0.0014297994125462643, -0.0010485540118683073,
    0.0025230775083682744, 0.0022092424122133704, 0.0017090829354326192, -0.00023623221225513262, -0.00075010754406605953, -0.00048451618995803595,
    0.00066678366052454186, 3.7043536695807748e-05, 0.00014817414678323156, 2.2992574043357255e-05, 1.2773652246309539e-06, 5.1094608985238367e-06,
    0.0011266345383882978, 6.2590807688238552e-05, 0.00025036323075295518, 0.013969917649369487, 0.00077610653607608254, 0.0031176780297960467,
    0.0004817220111243397, 2.6762333951352203e-05, 0.00010750629805031846, 0.023604345238211838, 0.0013113525132339909, 0.0052678011713415789,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, -0.00019873298405873342, -0.00017488502597168547, -0.00013513842915993879,
    0.00030472390257250655, 0.00026815703426380581, 0.00020721225374930455, -0.00010599091851377306, -9.3272008292120338e-05, -7.2073824589365728e-05,
    -0.00023155853167440414, -0.00020377150787347569, -0.00015745980153859487, 0.00035505640787303892, 0.00031244963892827429, 0.00024143835735366656,
    -0.00012349787619863472, -0.00010867813105479858, -8.3978555815071663e-05, -0.00015112572967159417, -0.00013299064211100294, -0.00010276549617668421,
    0.00023172611402562019, 0.00020391898034254587, 0.00015757375753742197, -8.060038435402599e-05, -7.0928338231542908e-05, -5.4808261360737768e-05,
    -2.7811858435545096e-05, -2.4474435423279694e-05, -1.8912063736170697e-05, 4.264484871705398e-05, 3.752746687100752e-05, 2.899849712759675e-05,
    -1.4832990281508884e-05, -1.3053031447727821e-05, -1.0086433391426055e-05, -9.1003422267808064e-05, -8.0083011595671172e-05, -6.188232714210941e-05,
    0.00013953857791771816, 0.00012279394856759207, 9.4886232984048218e-05, -4.8535155649910091e-05, -4.2710936971920918e-05, -3.3003905841938808e-05,
    -1.7497917479093762e-05, -1.5398167381602523e-05, -1.1898583885783741e-05, 2.6830139578366621e-05, 2.3610522828962645e-05, 1.8244494913289275e-05,
    -9.3322220992728581e-06, -8.2123554473601222e-06, -6.3459110275055338e-06, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    -0.00046041790739107262, -0.0004051677585041441, -0.00031308417702592987, 0.00070597411003002009, 0.00062125721682641821, 0.00048006239482041455,
    -0.00024555620263894763, -0.00021608945832227402, -0.00016697821779448465, -9.5938622148840694e-05, -8.4425987490979835e-05, -6.5238263061211772e-05,
    0.00014710588424508237, 0.00012945317813567258, 0.00010003200128665619, -5.1167262096241696e-05, -4.5027190644692715e-05, -3.4793738225444415e-05,
    0.00097139020741997056, -9.1746884706931479e-05, 0.00021673961305192834, 0.00031059340710623442, 0.00024068179590836186, 0.00019589984839241486,
    0.0018456839258025081, 2.4824396594609263e-05, 0.00050518637410966634, -0.00049834719327911125, -0.0011358878794392345, -0.00067776901392557125,
    0.0020746770511310911, 0.0018016694815019671, 0.0013990944146311109, 0.00070078971761825773, -0.00056157323707837733, -9.6075210799406309e-05,
    0.00012303022261623485, -4.7207760219010089e-05, -9.2066814719362303e-06, 9.7643327470694279e-05, 8.056493554668012e-05, 6.3195139566983222e-05,
    0.00027676526161214687, -1.9144637850553566e-05, 3.1286766767651744e-05, 1.7977379632957141e-05, 9.9568222858673291e-07, 3.9892112106288157e-06,
    6.1991055988255726e-07, 3.4333920759884602e-08, 1.3755921082831923e-07, 3.037557457283011e-05, 1.6823597433428067e-06, 6.7403918195664863e-06,
    0.00061238069252911862, 3.4021149584951025e-05, 0.0001371070819999282, 2.1116606853595674e-05, 1.1731448251997595e-06, 4.7278374102209628e-06,
    0.0010347122757967701, 5.7484015322042775e-05, 0.00023166370621205428, 0.019776072456853196, 0.00073997550490023103, 0.0049410792941349587,
    0.00097262899551031211, 0.00028132755344046446, 0.00036805452505038748, 0.033629143255501009, 0.0014389736246331081, 0.0084945112922410783,
    0.014560533984669867, -0.0067962370162234897, -0.00037457150992820651, 0.014272575097682367, 0.0118836751914206, 0.0093510148655294566,
    0.034758523356154557, -0.0025458065263279485, 0.0062733465376137443, 0.017235777268342314, -0.0069627852032329175, -0.0015249916229077465,
    0.025523621036559765, 0.007776798321391931, 0.0098338195832864666, 0.047508784805314737, -0.0058519525866809778, 0.0047148772187429865,
    0.020659428305519069, 0.00038674056507797991, 0.0034667254726354784, 0.0075616839655221228, 0.0040957148624301623, 0.0015712272015089923,
    0.039958914715862416, 0.0036643628656512143, 0.0069282419311995948, 0.018535757145528204, 0.00063618943215307982, 0.0033876768804494521,
    0.00063916498536118415, 2.193759909004654e-05, 0.00011681661702302115, 0.031319040090007029, 0.001074940838618401, 0.0057240061572765557,
    0.00068598199918343619, 1.9599485690955363e-05, 0.00011759691414573195, 2.3654586700921215e-05, 6.7584533431203611e-07, 4.0550720058722088e-06,
    0.0011590731128365234, 3.3116374652472165e-05, 0.00019869824791483263, -0.0013543888036952679, -0.0012378207985926616, -0.00094843619838750378,
    0.0021613574757677399, 0.0019004097952496706, 0.0014687764679186491, -0.00065992437820553513, -0.0006583877311179538, -0.00049513267629681338,
    -0.01134016755596773, -0.0096383472890208596, -0.0077397941713178544, 0.015058981101432087, -0.0064270478448617083, 0.0093972997256979926,
    -0.0077660153308347366, -0.020780543504257339, -0.005949889355549378, -0.0027171808831443663, -0.0032563303976423171, -0.0022162866960264748,
    0.0042792219372296781, 0.021949634055419782, 0.0068060013859907063, -0.0013659114065273149, 0.010769402470626197, 0.0013312810336351081,
    0.00086743513959195448, 2.4783697853697416e-05, 0.00014870306925902021, 2.9911901460292893e-05, 8.5487611483848853e-07, 5.1279040792440375e-06,
    0.0014656665929158432, 4.1876101287695269e-05, 0.00025125707819912248, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, -0.00019795748883377057, -0.00024869351704619126, -0.00018010393092239847,
    -0.0053229646865410821, -0.0011831405115486195, -0.0014814557220560161, -0.0042553276475993489, -0.0012864911174949648, -0.001392361190159479,
    -0.00062743426407494958, -0.00063487723167676572, -0.0004578336644844102, -0.0021069338737058087, 0.0020344411973969691, 3.2665961099621749e-05,
    -0.0025981317656597171, 0.0004438979149613613, -0.00073784497086818173, -2.8881551486147865e-10, -2.5415765307808025e-10, -1.963945501057846e-10,
    4.4285044693973594e-10, 3.8970839330693559e-10, 3.0113830391898837e-10, -1.5403493207825732e-10, -1.3555074022885529e-10, -1.0474375381320383e-10,
    -0.0015273293795758898, -0.0013440498540267841, -0.0010385839781116054, 0.0023419050001305384, 0.0020608764001148749, 0.0015924954000887659,
    -0.00081457562055464792, -0.0007168265460880906, -0.0005539114219771606, -0.00054474764918951328, -0.00047949538800698855, -0.00037049856016094473,
    0.00083549599562949856, 0.00073523242591623387, 0.00056813485775854527, -0.00029037254471429722, -0.0002557263007170827, -0.00019757187444462539,
    1.3225754750330516e-06, 3.7787870715230298e-08, 2.2672722429138016e-07, 4.5606118352845527e-08, 1.3030319529384522e-09, 7.8181917176306591e-09,
    2.2346966460236202e-06, 6.3848475600675274e-08, 3.8309085360404899e-07, 7.9325769308569054e-09, 2.2664505516734878e-10, 1.3598703310040548e-09,
    2.7353754033785579e-10, 7.8153582953676054e-12, 4.689214977220434e-11, 1.3403320563816798e-08, 3.8295201610906586e-10, 2.2977120966543317e-09,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0.01366391594586305, 0.00032404809899889189, 0.0023027540105083994,
    0.00059334583335118917, 0.00011868863514987414, 0.0001624848503048064, 0.023177416850836963, 0.00062682551242412778, 0.0039521346186241453,
    0.0012844982171866129, -2.2746613710063643e-05, 0.0001846913447993049, 0.00015375769679525192, 9.5544471412762727e-05, 8.0804597194809901e-05,
    0.0022510933624934741, 3.2612121625167361e-05, 0.00036696393397308587, 0.0013158304086419181, -0.000530938121793683, -0.00011353940841255753,
    0.0010888960269278525, 0.00098611426635665566, 0.00073438770937468451, 0.0029929359921354367, -0.00015630390036395448, 0.00035268303063743683,
    0.0072686092996565718, 0.00013993414595418467, 0.0012070350177465188, 0.00036526380066160126, 0.00036406056517252513, 0.00020568744181749719,
    0.012365981881430678, 0.00050138967000271822, 0.002160477426428943, 0.012729815698594464, 0.00036370901995984187, 0.0021822541197590501,
    0.00043895981160961854, 1.2541708903131962e-05, 7.5250253418791715e-05, 0.021509000418624101, 0.0006145428691035459, 0.0036872572146212728,
    0.0012802207808118751, 3.6577736594625076e-05, 0.00021946641956775007, 4.4145609494248032e-05, 1.2613031284070891e-06, 7.5678187704425221e-06,
    0.0021631318129338079, 6.1803766083823214e-05, 0.0003708225965029386, 0.0001177904443735356, -1.7383581005452658e-05, 7.7989363161478793e-06,
    4.2268886391483659e-05, 3.3022849294411968e-05, 2.6249785061790521e-05, 0.00022720441753241384, -4.5745897661755473e-06, 3.2339349758764401e-05,
    -0.0057125016196709032, -0.0051008791734522031, -0.0039153721398167227, 0.0069978332144160563, 0.0088917950185647601, 0.0057683175763941566,
    -0.0043457172522640584, -0.0019309746112150387, -0.0022617060268396564, -0.0084420383521978063, -0.0077383090320354408, -0.0058996670092941106,
    0.011530017039819101, 0.015085007311713906, 0.0095834512318767537, -0.0055456228303039645, -0.0017525242081994719, -0.0027502138122013566,
    0.00061555520716625794, -0.00015420654371643716, 2.9086400943691831e-06, 0.00033756634150331735, 0.00027306196135616509, 0.00021521168063942959,
    0.0012733886822900841, -5.5240839612309074e-05, 0.00016356714743071411, -4.3147815787460966e-05, -3.7970077892965675e-05, -2.9340514735473416e-05,
    6.615998283580802e-05, 5.8220784895511102e-05, 4.4988788328349394e-05, -2.3012167048347058e-05, -2.0250707002545427e-05, -1.5648273592875978e-05,
    -8.1519808292751771e-06, -7.1737431297621616e-06, -5.5433469639071123e-06, 1.2499703679077449e-05, 1.0999739237588162e-05, 8.4997985017726523e-06,
    -4.3477228498022699e-06, -3.825996107826001e-06, -2.9564515378655388e-06, -0.00087901115953998882, -0.00079114840028623459, -0.00060450044973833436,
    0.00058056715201619221, 0.0013389711304820191, 0.0007131096738903209, -0.0010346809683363138, -0.00032910691384373231, -0.000480078943988005,
    -0.0079995633279921104, -0.007062771425819998, -0.0054483216300510772, 0.01149874691728652, 0.011207213453983702, 0.008224219957628745,
    -0.0048323085651545208, -0.0034882945791075599, -0.0030015575031789882, -0.00061197775570230638, -0.0005385404250180288, -0.00041614487387756804,
    0.00093836587262262057, 0.00082576196790790524, 0.00063808879338338176, -0.00032638811692031441, -0.00028722154288987633, -0.00022194391950581372,};
const std::vector<double> proX5DerivDensity = {
    8.2419226003096823e-05, 0.00020704138724542527, 7.313533261850576e-05, -0.00027309976449606081, -8.9496181370967017e-05,};

/// Compares with a tolerance relative to the largest reference value (Pro stores the cluster orientation in single precision).
bool checkRelative(const QString& name, const std::vector<double>& actual, const std::vector<double>& expected)
{
    double scale = 0, maxDiff = 0;
    for(double v : expected) scale = std::max(scale, std::abs(v));
    bool ok = actual.size() == expected.size();
    for(size_t i = 0; ok && i < actual.size(); i++)
        maxDiff = std::max(maxDiff, std::abs(actual[i] - expected[i]));
    ok = ok && maxDiff <= 1e-6 * scale;
    const QString detail = QStringLiteral("%1 values, max deviation %2 (relative %3)").arg(actual.size()).arg(maxDiff).arg(scale > 0 ? maxDiff / scale : 0);
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail << "expected" << expected.size() << "values";
    return ok;
}

/// All components of a property, element-major.
std::vector<double> allValues(const Property* property)
{
    std::vector<double> values;
    if(!property)
        return values;
    values.resize(property->size() * property->componentCount());
    property->copyTo(values.begin());
    return values;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void SpatialBinningSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("spatial-binning-selftest")},
        tr("Run the in-process regression test for the Spatial binning modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void SpatialBinningSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("spatial-binning-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Spatial binning self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Spatial binning self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Spatial binning self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool SpatialBinningSelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dumpPath = dir.filePath(QStringLiteral("spatial_binning_test.dump"));
    writeDump(dumpPath);

    QUrl url = QUrl::fromLocalFile(dumpPath);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test dump."));
    importer->setMultiTimestepFile(true);
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);

    // Only consulted when the binning is restricted to selected particles.
    OORef<ExpressionSelectionModifier> selection = OORef<ExpressionSelectionModifier>::create();
    selection->setExpression(QStringLiteral("ParticleIdentifier <= 4"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, selection);

    OORef<SpatialBinningModifier> binning = OORef<SpatialBinningModifier>::create();
    // Memorized fields: set every one explicitly so GUI defaults cannot leak into the test.
    binning->setSourceProperty(PropertyReference(QStringLiteral("Velocity.X")));
    binning->setBinDirection(SpatialBinningModifier::X);
    binning->setNumberOfBinsX(2);
    binning->setNumberOfBinsY(2);
    binning->setNumberOfBinsZ(2);
    binning->setReductionOperation(SpatialBinningModifier::Mean);
    binning->setFixPropertyAxisRange(false);
    ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, binning);

    auto evaluate = [&](int frame) {
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
    };
    auto table = [&](int frame) -> std::pair<std::vector<double>, std::vector<double>> {
        PipelineFlowState state = evaluate(frame);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        if(!t)
            throw Exception(QStringLiteral("The binning table is missing from the pipeline output: %1").arg(state.status().text()));
        ConstPropertyPtr x = t->getXValues();
        return { column(x), column(t->y()) };
    };
    auto grid = [&](int frame) -> std::pair<std::vector<double>, std::vector<double>> {
        PipelineFlowState state = evaluate(frame);
        const VoxelGrid* g = state.getObjectBy<VoxelGrid>(node, QStringLiteral("binning"));
        if(!g || g->properties().empty())
            throw Exception(QStringLiteral("The binning grid is missing from the pipeline output: %1").arg(state.status().text()));
        return { {(double)g->shape()[0], (double)g->shape()[1], (double)g->shape()[2]}, column(g->properties().back()) };
    };

    bool ok = true;

    // Two x bins [0,5) and [5,10): atoms 1 2 3 6 (vx 1 3 5 2) and 4 5 7 8 (vx 7 9 4 6, atom 5 wrapped).
    qInfo().noquote() << "Spatial binning self-test: 1D reductions of Velocity.X (periodic, 2 bins)";
    const std::vector<std::pair<SpatialBinningModifier::ReductionOperation, std::vector<double>>> expected1D = {
        {SpatialBinningModifier::Mean, {2.75, 6.5}},
        {SpatialBinningModifier::Sum, {11, 26}},
        {SpatialBinningModifier::SumVol, {11.0 / 500, 26.0 / 500}},
        {SpatialBinningModifier::Min, {1, 4}},
        {SpatialBinningModifier::Max, {5, 9}}};
    for(const auto& [op, values] : expected1D) {
        binning->setReductionOperation(op);
        ok &= check(QStringLiteral("op %1").arg((int)op), table(0).second, values);
    }
    ok &= check(QStringLiteral("bin centers"), table(0).first, {2.5, 7.5});

    qInfo().noquote() << "Spatial binning self-test: particle count without input property";
    binning->setSourceProperty({});
    binning->setReductionOperation(SpatialBinningModifier::Sum);
    ok &= check(QStringLiteral("count (periodic)"), table(0).second, {4, 4});
    ok &= check(QStringLiteral("count (non-periodic x drops atom 5)"), table(1).second, {4, 3});

    qInfo().noquote() << "Spatial binning self-test: only selected particles (ids 1-4)";
    binning->setSourceProperty(PropertyReference(QStringLiteral("Velocity.X")));
    binning->setOnlySelectedElements(true);
    ok &= check(QStringLiteral("sum of selected"), table(0).second, {9, 7});
    binning->setOnlySelectedElements(false);

    // Five bins of width 2: mean vx = 2 5 3 6 8; periodic central differences with h = 2.
    qInfo().noquote() << "Spatial binning self-test: periodic first derivative (5 bins)";
    binning->setNumberOfBinsX(5);
    binning->setReductionOperation(SpatialBinningModifier::Mean);
    ok &= check(QStringLiteral("mean profile"), table(0).second, {2, 5, 3, 6, 8});
    binning->setFirstDerivative(true);
    ok &= check(QStringLiteral("derivative"), table(0).second, {-0.75, 0.25, 0.25, 1.25, -1});
    // Non-periodic x (frame 1): one-sided differences at the ends. Values measured in OVITO Pro 3.16.1.
    ok &= check(QStringLiteral("derivative (non-periodic)"), table(1).second, {1.5, 0.25, 0.25, 1.0, 0.5});
    {
        PipelineFlowState state = evaluate(0);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        ok &= check(QStringLiteral("derivative column named as in Pro"), {t && t->y()->name() == QStringLiteral("d(Velocity.X)/d(Position)") ? 1.0 : 0.0}, {1});
    }
    binning->setFirstDerivative(false);
    {
        PipelineFlowState state = evaluate(0);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        ok &= check(QStringLiteral("table title and x label as in Pro"),
            {t && t->title() == QStringLiteral("Velocity.X") && t->axisLabelX() == QStringLiteral("Position") && t->plotMode() == DataTable::Histogram ? 1.0 : 0.0}, {1});
    }
    binning->setNumberOfBinsX(2);

    // As in OVITO Pro, a 1D binning along Y takes its bin count from the first field (2 bins, not 4).
    qInfo().noquote() << "Spatial binning self-test: 1D along Y uses the first bin count";
    binning->setBinDirection(SpatialBinningModifier::Y);
    binning->setNumberOfBinsY(4);
    ok &= check(QStringLiteral("Y mean (2 bins)"), table(0).second, {29.0 / 6.0, 4});
    binning->setNumberOfBinsY(2);
    binning->setBinDirection(SpatialBinningModifier::X);

    // As in OVITO Pro 3.16.1 (measured): the x-axis runs from 0 to the length of the binned cell vector,
    // ignoring the cell origin, and the derivative step is that length divided by the bin count.
    qInfo().noquote() << "Spatial binning self-test: x-axis range with shifted origin and triclinic cell";
    auto interval = [&](int frame) -> std::vector<double> {
        PipelineFlowState state = evaluate(frame);
        const DataTable* t = state.getObjectBy<DataTable>(node, QStringLiteral("binning"));
        if(!t)
            throw Exception(QStringLiteral("The binning table is missing from the pipeline output: %1").arg(state.status().text()));
        return { t->intervalStart(), t->intervalEnd() };
    };
    ok &= check(QStringLiteral("range (shifted origin)"), interval(2), {0, 10});
    binning->setBinDirection(SpatialBinningModifier::Y);
    ok &= check(QStringLiteral("range along tilted b"), interval(3), {0, std::sqrt(116.0)});
    binning->setBinDirection(SpatialBinningModifier::X);

    // Voxel index = z*4 + y*2 + x.
    qInfo().noquote() << "Spatial binning self-test: 2D (XY) max and 3D count";
    binning->setBinDirection(SpatialBinningModifier::XY);
    binning->setReductionOperation(SpatialBinningModifier::Max);
    {
        auto [shape, values] = grid(0);
        ok &= check(QStringLiteral("2D shape"), shape, {2, 2, 1});
        ok &= check(QStringLiteral("2D max"), values, {5, 9, 2, 6});
    }
    binning->setBinDirection(SpatialBinningModifier::XYZ);
    binning->setSourceProperty({});
    binning->setReductionOperation(SpatialBinningModifier::Sum);
    {
        auto [shape, values] = grid(0);
        ok &= check(QStringLiteral("3D shape"), shape, {2, 2, 2});
        ok &= check(QStringLiteral("3D count"), values, {3, 2, 1, 1, 0, 1, 0, 0});
    }

    // XZ with bin counts (2, 4, 3): the grid is 2 x 4 x 1 (x along the first axis, z along the second),
    // the derivative option is ignored, and the domain is a 2D cell. Values measured in OVITO Pro 3.16.1.
    qInfo().noquote() << "Spatial binning self-test: 2D (XZ) grid layout as in OVITO Pro";
    binning->setBinDirection(SpatialBinningModifier::XZ);
    binning->setSourceProperty(PropertyReference(QStringLiteral("Velocity.X")));
    binning->setNumberOfBinsY(4);
    binning->setNumberOfBinsZ(3);
    binning->setFirstDerivative(true);
    for(const auto& [op, values] : std::vector<std::pair<SpatialBinningModifier::ReductionOperation, std::vector<double>>>{
            {SpatialBinningModifier::Min, {1, 6, 2, 0, 0, 0, 0, 4}},
            {SpatialBinningModifier::Mean, {3, 22.0 / 3.0, 2, 0, 0, 0, 0, 4}},
            {SpatialBinningModifier::Sum, {9, 22, 2, 0, 0, 0, 0, 4}}}) {
        binning->setReductionOperation(op);
        auto [shape, actual] = grid(0);
        ok &= check(QStringLiteral("XZ shape"), shape, {2, 4, 1});
        ok &= check(QStringLiteral("XZ op %1").arg((int)op), actual, values);
    }
    {
        PipelineFlowState state = evaluate(0);
        const VoxelGrid* g = state.getObjectBy<VoxelGrid>(node, QStringLiteral("binning"));
        const AffineTransformation& m = g->domain()->cellMatrix();
        ok &= check(QStringLiteral("XZ domain vectors"), {m(0, 0), m(2, 1), m(2, 2), g->domain()->is2D() ? 1.0 : 0.0}, {10, 10, 1, 1});
    }


    // Dislocation mode: trilinear (cloud-in-cell) weights between bin centers, integrated exactly along each segment.
    qInfo().noquote() << "Spatial binning self-test: dislocation density and Nye tensor compared with OVITO Pro";
    {
        const QString caPath = dir.filePath(QStringLiteral("lines.ca"));
        writeCA(caPath, testLines);
        auto run = [&](const QString& path, SpatialBinningModifier::BinDirection direction, std::array<int, 3> bins, bool derivative) -> std::pair<std::vector<double>, std::vector<double>> {
            const QUrl caUrl = QUrl::fromLocalFile(path);
            OORef<FileSource> src = OORef<FileSource>::create();
            src->setSource({caUrl}, dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(caUrl).blockForResult()), false);
            (void) src->updateListOfFrames().blockForResult();
            OORef<Pipeline> p = OORef<Pipeline>::create();
            p->setHead(src);
            OORef<SpatialBinningModifier> mod = OORef<SpatialBinningModifier>::create();
            mod->setOperateOn(SpatialBinningModifier::OperateOnDislocations);
            mod->setBinDirection(direction);
            mod->setNumberOfBinsX(bins[0]);
            mod->setNumberOfBinsY(bins[1]);
            mod->setNumberOfBinsZ(bins[2]);
            mod->setFirstDerivative(derivative);
            mod->setFixPropertyAxisRange(false);
            ModificationNode* n = p->applyModifier(AnimationTime::fromFrame(0), false, mod);
            PipelineFlowState state = p->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
            const PropertyContainer* container = state.getObjectBy<DataTable>(n, QStringLiteral("binning"));
            if(!container) container = state.getObjectBy<VoxelGrid>(n, QStringLiteral("binning"));
            if(!container)
                throw Exception(QStringLiteral("The binning output is missing: %1").arg(state.status().text()));
            return {allValues(container->getProperty(QStringLiteral("Density"))), allValues(container->getProperty(QStringLiteral("Nye Tensor")))};
        };
        auto [d1, n1] = run(caPath, SpatialBinningModifier::X, {5, 1, 1}, false);
        ok &= checkRelative(QStringLiteral("1D X (5 bins) density"), d1, proX5Density);
        ok &= checkRelative(QStringLiteral("1D X (5 bins) Nye tensor"), n1, proX5Nye);
        auto [d2, n2] = run(caPath, SpatialBinningModifier::XY, {4, 3, 1}, false);
        ok &= checkRelative(QStringLiteral("2D XY (4x3) density"), d2, proXY43Density);
        ok &= checkRelative(QStringLiteral("2D XY (4x3) Nye tensor"), n2, proXY43Nye);
        auto [d3, n3] = run(caPath, SpatialBinningModifier::XYZ, {5, 4, 3}, false);
        ok &= checkRelative(QStringLiteral("3D (5x4x3) density"), d3, proXYZ543Density);
        ok &= checkRelative(QStringLiteral("3D (5x4x3) Nye tensor"), n3, proXYZ543Nye);

        // The first derivative applies to both outputs (Pro: periodic central differences, same names).
        auto [dd, nd] = run(caPath, SpatialBinningModifier::X, {5, 1, 1}, true);
        ok &= checkRelative(QStringLiteral("1D derivative of the density"), dd, proX5DerivDensity);
        std::vector<double> nyeDerivative(n1.size());
        for(size_t b = 0; b < 5; b++)
            for(size_t c = 0; c < 9; c++)
                nyeDerivative[b * 9 + c] = (n1[((b + 1) % 5) * 9 + c] - n1[((b + 4) % 5) * 9 + c]) / (2 * 8.0);
        ok &= checkRelative(QStringLiteral("1D derivative of the Nye tensor"), nd, nyeDerivative);

        // Deliberate difference from Pro, which rejects points between a non-periodic cell boundary and the first bin center:
        // such a point gives its full weight to the boundary bin. A z line at x = 2 (first bin center at x = 4) keeps its full length.
        const QString nonPeriodicPath = dir.filePath(QStringLiteral("nonperiodic.ca"));
        writeCA(nonPeriodicPath, {{{0.5, 0.0, 0.5}, {{2, 10, 0}, {2, 10, 20}}}}, false);
        auto [dn, nn] = run(nonPeriodicPath, SpatialBinningModifier::X, {5, 1, 1}, false);
        ok &= checkRelative(QStringLiteral("non-periodic x: line near the boundary"), dn, {20.0 / 4800.0, 0, 0, 0, 0});

        // Errors (messages as in OVITO Pro).
        auto expectError = [&](const QString& name, const QString& path, const QString& message) {
            QString error;
            try { run(path, SpatialBinningModifier::X, {5, 1, 1}, false); }
            catch(const Exception& ex) { error = ex.messages().join(' '); }
            ok &= check(name + QStringLiteral(": ") + (error.isEmpty() ? QStringLiteral("no error") : error), {error.contains(message) ? 1.0 : 0.0}, {1});
        };
        const QString outsidePath = dir.filePath(QStringLiteral("outside.ca"));
        writeCA(outsidePath, {{{0.5, 0.0, 0.5}, {{-1, 10, 0}, {2, 10, 20}}}}, false);
        expectError(QStringLiteral("point outside a non-periodic boundary"), outsidePath, QStringLiteral("located outside the simulation domain in a non-periodic direction"));
        const QString triclinicPath = dir.filePath(QStringLiteral("triclinic.ca"));
        writeCA(triclinicPath, testLines, true, 5);
        expectError(QStringLiteral("triclinic cell"), triclinicPath, QStringLiteral("orthogonal and axis-aligned"));
        expectError(QStringLiteral("no dislocations"), dumpPath, QStringLiteral("contains no Dislocations data object"));

        // Every segment deposits its full length, for any bin count (including 1), lines spanning several periods
        // and a line lying on a bin-center plane (x = 4 is the first center for 5 bins over 40). Cell volume 24000.
        const QString conservePath = dir.filePath(QStringLiteral("conserve.ca"));
        writeCA(conservePath, {{{0.5, 0.0, 0.5}, {{-50, 10, 4}, {70, 12, 6}}}, {{0.0, 0.5, -0.5}, {{4, 1, 1}, {4, 29, 19}}}});
        const double totalLength = std::sqrt(120.0 * 120 + 4 + 4) + std::sqrt(28.0 * 28 + 18 * 18);
        struct ConservationCase { SpatialBinningModifier::BinDirection direction; std::array<int, 3> bins; const char* name; };
        for(const ConservationCase& c : {ConservationCase{SpatialBinningModifier::X, {5, 1, 1}, "X5"}, ConservationCase{SpatialBinningModifier::X, {1, 1, 1}, "X1"},
                ConservationCase{SpatialBinningModifier::XY, {4, 3, 1}, "XY43"}, ConservationCase{SpatialBinningModifier::XYZ, {1, 1, 1}, "XYZ111"},
                ConservationCase{SpatialBinningModifier::XYZ, {7, 5, 3}, "XYZ753"}}) {
            auto [density, nye] = run(conservePath, c.direction, c.bins, false);
            double sum = 0;
            for(double v : density) sum += v;
            ok &= checkRelative(QStringLiteral("length conservation %1").arg(c.name), {sum * 24000.0 / density.size()}, {totalLength});
        }

        // Deliberate difference from Pro: a non-periodic axis that is not binned accepts any point inside the cell.
        const QString unbinnedPath = dir.filePath(QStringLiteral("unbinned_nonperiodic.ca"));
        writeCA(unbinnedPath, {{{0.5, 0.0, 0.5}, {{1, 5, 2}, {1, 25, 18}}}}, false);
        auto [dy, ny] = run(unbinnedPath, SpatialBinningModifier::Y, {5, 1, 1}, false);
        double sumY = 0;
        for(double v : dy) sumY += v;
        ok &= checkRelative(QStringLiteral("non-periodic unbinned x accepted"), {sumY * 24000.0 / dy.size()}, {std::sqrt(400.0 + 256.0)});
    }

    qInfo().noquote() << (ok ? "Spatial binning self-test: ALL PASSED" : "Spatial binning self-test: FAILED");
    return ok;
}

}   // End of namespace
