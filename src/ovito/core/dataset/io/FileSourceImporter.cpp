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

#include <ovito/core/Core.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/dataset/scene/Scene.h>
#include <ovito/core/dataset/data/DataObject.h>
#include <ovito/core/dataset/data/AttributeDataObject.h>
#include <ovito/core/dataset/scene/SelectionSet.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include <ovito/core/dataset/animation/AnimationSettings.h>
#include <ovito/core/viewport/ViewportConfiguration.h>
#include <ovito/core/utilities/io/FileManager.h>
#include <ovito/core/utilities/concurrent/Launch.h>
#include <ovito/core/utilities/concurrent/ForEach.h>
#include <ovito/core/app/UserInterface.h>
#include <ovito/core/app/Application.h>
#include "FileSourceImporter.h"
#include "FileSource.h"

#include <QStringBuilder>

namespace Ovito {

IMPLEMENT_ABSTRACT_OVITO_CLASS(FileSourceImporter);
DEFINE_PROPERTY_FIELD(FileSourceImporter, isMultiTimestepFile);
SET_PROPERTY_FIELD_LABEL(FileSourceImporter, isMultiTimestepFile, "File contains multiple timesteps");

/******************************************************************************
* Is called when the value of a property of this object has changed.
******************************************************************************/
void FileSourceImporter::propertyChanged(const PropertyFieldDescriptor* field)
{
    FileImporter::propertyChanged(field);

    if(field == PROPERTY_FIELD(isMultiTimestepFile)) {
        // Automatically re-scan input file for animation frames when this option is changed.
        requestFramesUpdate();

        // Also notify the UI explicitly, because target-changed messages are suppressed for this property field.
        notifyDependents(FileSourceImporter::MultiTimestepFileChanged);
    }
}

/******************************************************************************
* Sends a request to the FileSource owning this importer to reload
* the input file.
******************************************************************************/
void FileSourceImporter::requestReload(bool refetchFiles, int frame)
{
    OVITO_ASSERT_MSG(this_task::isMainThread(), "FileSourceImporter::requestReload", "This function may only be called from the main thread.");
    OVITO_ASSERT(this_task::get());

    // Retrieve the FileSource that owns this importer by looking it up in the list of dependents.
    visitDependents([&](RefMaker* dependent) {
        if(FileSource* fileSource = dynamic_object_cast<FileSource>(dependent)) {
            this_task::ui()->handleExceptions([&] {
                fileSource->reloadFrame(refetchFiles, frame);
            });
        }
        else if(FileSourceImporter* parentImporter = dynamic_object_cast<FileSourceImporter>(dependent)) {
            // If this importer is a child of another importer, forward the reload request to the parent importer.
            parentImporter->requestReload(refetchFiles, frame);
        }
    });
}

/******************************************************************************
* Sends a request to the FileSource owning this importer to refresh the
* animation frame sequence.
******************************************************************************/
void FileSourceImporter::requestFramesUpdate(bool refetchCurrentFile)
{
    OVITO_ASSERT_MSG(this_task::isMainThread(), "FileSourceImporter::requestReload", "This function may only be called from the main thread.");
    OVITO_ASSERT(this_task::get());

    // Retrieve the FileSource that owns this importer by looking it up in the list of dependents.
    visitDependents([&](RefMaker* dependent) {
        if(FileSource* fileSource = dynamic_object_cast<FileSource>(dependent)) {
            // Scan input source for animation frames.
            this_task::ui()->handleExceptions([&] {
                fileSource->updateListOfFrames(refetchCurrentFile);
            });
        }
        else if(FileSourceImporter* parentImporter = dynamic_object_cast<FileSourceImporter>(dependent)) {
            // If this importer is a child of another importer, forward the update request to the parent importer.
            parentImporter->requestFramesUpdate(refetchCurrentFile);
        }
    });
}

/******************************************************************************
* Returns the FileSource that manages this importer object (if any).
******************************************************************************/
FileSource* FileSourceImporter::fileSource() const
{
    FileSource* source = nullptr;
    visitDependents([&](RefMaker* dependent) {
        if(FileSource* fileSource = dynamic_object_cast<FileSource>(dependent))
            source = fileSource;
    });
    return source;
}

/******************************************************************************
* Determines if the option to replace the currently selected object
* with the new file is available.
******************************************************************************/
bool FileSourceImporter::isReplaceExistingPossible(Scene* scene, const std::vector<QUrl>& sourceUrls)
{
    if(scene) {
        // Look for an existing FileSource in the scene whose
        // data source we can replace with the new file.
        for(SceneNode* node : scene->selection()->nodes()) {
            if(Pipeline* pipeline = node->pipeline()) {
                if(dynamic_object_cast<FileSource>(pipeline->source()))
                    return true;
            }
        }
    }
    return false;
}

/******************************************************************************
* Imports the data from a file into the scene.
* Return true if the file has been imported.
* Return false if the import has been aborted by the user.
* Throws an exception when the import has failed.
******************************************************************************/
Future<OORef<Pipeline>> FileSourceImporter::importFileSet(OORef<Scene> scene, std::vector<std::pair<QUrl, OORef<FileImporter>>> sourceUrlsAndImporters, ImportMode importMode, bool autodetectFileSequences, MultiFileImportMode multiFileImportMode)
{
    OVITO_ASSERT(!sourceUrlsAndImporters.empty());
    OORef<FileSource> existingFileSource;
    OORef<Pipeline> existingPipeline;

    if(importMode == ReplaceSelected) {
        // Look for an existing FileSource in the scene whose
        // data source can be replaced with the newly imported file.
        if(scene) {
            for(SceneNode* node : scene->selection()->nodes()) {
                if(Pipeline* pipeline = node->pipeline()) {
                    existingFileSource = dynamic_object_cast<FileSource>(pipeline->source());
                    if(existingFileSource) {
                        existingPipeline = pipeline;
                        break;
                    }
                }
            }
        }
    }
    else if(importMode == ResetScene) {
        if(scene)
            scene->deleteChildren();
    }
    else if(importMode == AddToScene) {
        OVITO_ASSERT(scene != nullptr);
        if(scene->children().empty())
            importMode = ResetScene;
        else {
#ifdef OVITO_BUILD_BASIC
            throw Exception(tr("Sorry, this operation cannot be performed in OVITO Basic. Importing multiple datasets into the same scene is supported by <a href=\"https://www.ovito.org/#proFeatures\">OVITO Pro</a>."));
#endif
        }
    }

    OORef<FileSource> fileSource = existingFileSource;

    // Create the object that will insert the imported data into the scene.
    if(!fileSource)
        fileSource = OORef<FileSource>::create();

    // Inherit multi-timestep flag from existing importer, but only if the old importer
    // was for the same file format. That's because the new importer may not support multi-timestep
    // files at all.
    if(existingFileSource && existingFileSource->importer() && existingFileSource->importer()->getOOClass() == this->getOOClass() && existingFileSource->importer()->isMultiTimestepFile())
        setMultiTimestepFile(true);

    // Create a new pipeline and a new node in the scene.
    OORef<Pipeline> pipeline;
    if(!existingPipeline) {
        OORef<SceneNode> sceneNode;
        {
            UndoSuspender undoSuspender;    // Do not create undo records for this part.
            AnimationSuspender animSuspender(*this_task::ui());

            // Add object to scene.
            pipeline = OORef<Pipeline>::create();
            pipeline->setHead(fileSource);
            sceneNode = OORef<SceneNode>::create();
            sceneNode->setPipeline(pipeline);
        }

        // Insert node into scene.
        if(importMode != DontAddToScene && scene != nullptr) {
            scene->addChildNode(sceneNode);

            // Select new object in the scene.
            if(this_task::isInteractive())
                scene->selection()->setNode(sceneNode);
        }
    }
    else pipeline = existingPipeline;

    // Concatenate all files from the input list having the same file format into one sequence,
    // which gets handled by this importer.
    std::vector<QUrl> sourceUrls;
    OVITO_ASSERT(sourceUrlsAndImporters.front().second == this);
    sourceUrls.push_back(std::move(sourceUrlsAndImporters.front().first));
    auto iter = std::next(sourceUrlsAndImporters.begin());
    if(multiFileImportMode == ImportAsTrajectory) {
        for(; iter != sourceUrlsAndImporters.end(); ++iter) {
            if(iter->second->getOOClass() != this->getOOClass())
                break;
            sourceUrls.push_back(std::move(iter->first));
        }
    }
    sourceUrlsAndImporters.erase(sourceUrlsAndImporters.begin(), iter);

    // Set the input file location(s) and importer.
    bool keepExistingDataCollection = true;
    auto urlCount = sourceUrls.size();
    fileSource->setSource(std::move(sourceUrls), this, autodetectFileSequences && (urlCount == 1 && sourceUrlsAndImporters.empty()), keepExistingDataCollection);

    // Let the importer subclass customize the pipeline.
    // Placing this after setSource allows a call to evaluatePipeline() in setupPipeline() implementation.
    Future<void> setupFuture = setupPipeline(pipeline, fileSource);
    if(setupFuture) {
        co_await FutureAwaiter(ObjectExecutor(this), std::move(setupFuture));
    }

    if(importMode != ReplaceSelected && importMode != DontAddToScene) {
        // Adjust viewports to completely show the newly imported object.
        // This needs to be deferred until after the data has been completely loaded and its extents are known.
        this_task::ui()->zoomToSceneExtentsWhenReady();
    }

    // If this importer did not handle all supplied input files,
    // continue importing the remaining files.
    if(!sourceUrlsAndImporters.empty()) {
        co_await FutureAwaiter(ObjectExecutor(this), importFurtherFiles(std::move(scene), std::move(sourceUrlsAndImporters), importMode, autodetectFileSequences, multiFileImportMode, pipeline));
    }

    co_return pipeline;
}

/******************************************************************************
* Is called when importing multiple files of different formats.
******************************************************************************/
Future<OORef<Pipeline>> FileSourceImporter::importFurtherFiles(OORef<Scene> scene, std::vector<std::pair<QUrl, OORef<FileImporter>>> sourceUrlsAndImporters, ImportMode importMode, bool autodetectFileSequences, MultiFileImportMode multiFileImportMode, Pipeline* pipeline)
{
    if(importMode == DontAddToScene)
        return Future<OORef<Pipeline>>::createImmediateEmplace();    // It doesn't make sense to import additional datasets if they are not being added to the scene. They would get lost.

    OVITO_ASSERT(!sourceUrlsAndImporters.empty());
    OORef<FileImporter> importer = sourceUrlsAndImporters.front().second;
    return importer->importFileSet(scene, std::move(sourceUrlsAndImporters), AddToScene, autodetectFileSequences, multiFileImportMode);
}

/******************************************************************************
* Determines whether the URL contains a wildcard pattern.
******************************************************************************/
bool FileSourceImporter::isWildcardPattern(const QString& filename)
{
    return filename.contains('*');
}

/******************************************************************************
* Tries to derive a sensible wildcard pattern from a filename by replacing a
* numeric character sequence with a '*'.
******************************************************************************/
QString FileSourceImporter::deriveWildcardPatternFromFilename(const QString& filename)
{
    int startIndex, endIndex;

    // Locate the first digit from the back of the filename.
    // If the filename has a regular format suffix (dot followed by three or less chars),
    // do not look for digits in the suffix. This exception is specifically needed for
    // compatiblity with file suffixes like *.h5 used by pyiron.

    // First, skip to last '.' in filename.
    for(endIndex = filename.length() - 2; endIndex >= 1; endIndex--)
        if(filename.at(endIndex) == QChar('.'))
            break;
    // If no dot was found, jump back to end of filename.
    if(endIndex <= 1 || endIndex + 4 < filename.length())
        endIndex = filename.length() - 1;

    // Then skip to last digit.
    for(; endIndex >= 0; endIndex--)
        if(filename.at(endIndex).isNumber())
            break;

    // If we have found a first digit, identify the contiguous range of digits
    // and replace this number with the placeholder (*).
    if(endIndex >= 0) {
        for(startIndex = endIndex-1; startIndex >= 0; startIndex--)
            if(!filename.at(startIndex).isNumber()) break;

        return filename.left(startIndex + 1) + QChar('*') + filename.mid(endIndex + 1);
    }

    return {};
}

/******************************************************************************
* Scans the given external path(s) (which may be a directory and a wild-card pattern,
* or a single file containing multiple frames) to find all available animation frames.
******************************************************************************/
Future<QVector<FileSourceImporter::Frame>> FileSourceImporter::discoverFrames(const std::vector<QUrl>& sourceUrls)
{
    OVITO_ASSERT_MSG(this_task::isMainThread(), "FileSourceImporter::discoverFrames", "This function may only be called from the main thread.");
    OVITO_ASSERT(this_task::get());

    // No output if there is no input.
    if(sourceUrls.empty())
        return QVector<Frame>();

    // If there is only a single input path, call sub-routine handling single paths.
    if(sourceUrls.size() == 1)
        return discoverFrames(sourceUrls.front());

    // Sequentially invoke single-path routine for each input path and compile results
    // into one big list that is returned to the caller.
    return for_each_sequential(
        sourceUrls,
        DeferredObjectExecutor(this),
        [this](const QUrl& url) {
            return discoverFrames(url);
        },
        [](const QUrl& url, QVector<Frame>&& frames, QVector<FileSourceImporter::Frame>& combinedList) {
            combinedList += std::move(frames);
        },
        QVector<FileSourceImporter::Frame>{});
}

/******************************************************************************
* Scans the given external path (which may be a directory and a wild-card pattern,
* or a single file containing multiple frames) to find all available animation frames.
******************************************************************************/
Future<QVector<FileSourceImporter::Frame>> FileSourceImporter::discoverFrames(const QUrl& sourceUrl)
{
    OVITO_ASSERT_MSG(this_task::isMainThread(), "FileSourceImporter::discoverFrames", "This function may only be called from the main thread.");
    OVITO_ASSERT(this_task::get());

    if(shouldScanFileForFrames(sourceUrl)) {
        // Check if filename is a wildcard pattern.
        // If yes, find all matching files and scan each one of them.
        if(isWildcardPattern(sourceUrl)) {
            return findWildcardMatchesResolved(sourceUrl)
                .then(ObjectExecutor(this), [this](const std::vector<QUrl>& fileList) {
                    return discoverFrames(fileList);
                });
        }

        // Fetch file, then scan it.
        return Application::instance()->fileManager().fetchUrl(sourceUrl)
            .then(ObjectExecutor(this), [this](const FileHandle& fileHandle) {
                return discoverFrames(fileHandle);
            });
    }
    else {
        if(isWildcardPattern(sourceUrl)) {
            // Find all files matching the file pattern.
            return findWildcardMatches(sourceUrl)
                .then(ObjectExecutor(this), [sourceUrl](const QStringList& entries) {

                    // Resolve list of matches into complete URLs.
                    std::vector<QUrl> urlList = FileSourceImporter::resolveWildcardMatches(sourceUrl, entries);
                    OVITO_ASSERT(urlList.size() == entries.size());

                    // Turn the file list into a animation frames list.
                    // Label each frame with the timestep number inferred from the numeric part of the filename.
                    QVector<Frame> frames;
                    frames.reserve(entries.size());
                    auto it = entries.cbegin();
                    for(QUrl& url : urlList)
                        frames.emplace_back(std::move(url), (it++)->toLongLong());
                    return frames;
                });
        }
        else {
            // Build just a single frame from the source URL.
            return QVector<Frame>{{ Frame(sourceUrl, 0, 1) }};
        }
    }
}

/******************************************************************************
* Scans the given external path (which may be a directory and a wild-card pattern,
* or a single file containing multiple frames) to find all available animation frames.
******************************************************************************/
Future<QVector<FileSourceImporter::Frame>> FileSourceImporter::discoverFrames(const FileHandle& fileHandle)
{
    return asyncLaunch([fileHandle, self=OORef<const FileSourceImporter>(this)]() {
        QVector<Frame> framesList;
        try {
            self->discoverFramesInFile(fileHandle, framesList);
        }
        catch(const Exception& ex) {
            // Silently ignore parsing and I/O errors if at least two frames have been read.
            // Keep all frames read up to where the error occurred.
            if(framesList.size() <= 1)
                throw;
            else {
                qWarning() << "WARNING: Scanning of the trajectory file" << fileHandle.localFilePath() << "aborted prematurely after" << framesList.size() << "frames due to an error:" << ex.messages().join(' ');
                framesList.pop_back();       // Remove last discovered frame because it may be corrupted or only partially written.
            }
        }

        // If it's not a trajectory file, report a single frame.
        if(framesList.empty())
            framesList.emplace_back(fileHandle);

        return framesList;
    });
}

/******************************************************************************
* Loads the data for the given frame from the external file.
******************************************************************************/
Future<PipelineFlowState> FileSourceImporter::loadFrame(const LoadOperationRequest& request)
{
    OVITO_ASSERT_MSG(this_task::isMainThread(), "FileSourceImporter::loadFrame", "This function may only be called from the main thread.");

    // Create the frame loader for the requested frame.
    FrameLoaderPtr frameLoader = createFrameLoader(request);
    if(!frameLoader)
        throw Exception(tr("Failed to create frame loader for file format '%1'.").arg(objectTitle()));

    // Execute the loader in a worker thread.
    return asyncLaunch([frameLoader=std::move(frameLoader), self=OORef<FileSourceImporter>(this)]() {

        // Let the subclass implementation parse the file.
        frameLoader->loadFile();

        // Stop the task if it has been canceled.
        this_task::throwIfCanceled();

        // If the parser has detected additional frames following the first frame in the
        // input file, automatically turn on scanning of the input file.
        // Only do this if the file is being newly imported by the user.
        if(frameLoader->additionalFramesDetected() && frameLoader->loadRequest().isNewlyImportedFile && !self->isMultiTimestepFile()) {
            // Note: Changing a parameter of the file importer must be done in the main thread.
            launchDetached(ObjectExecutor(self), [self, fileSource=dynamic_object_cast<FileSource>(frameLoader->pipelineNode().lock())]() {
                self->setMultiTimestepFile(true);
                // Also remove wildcard pattern if there is one. Typically, the user just wants to load
                // a single file if they selected a file containing multiple trajectory frames.
                if(fileSource)
                    const_cast<FileSource*>(fileSource.get())->setAutoGenerateFilePattern(false);
            });
        }

        // Pass the constructed pipeline state back to the caller.
        return std::move(frameLoader->state());
    });
}

/******************************************************************************
* Queries the filesystem and returns a list of files matching the given wildcard pattern.
* The filename pattern must contain exactly one '*' wildcard character, which represents a sequence of digits of arbitrary length.
* The function returns the numeric part extracted from each matching filename.
******************************************************************************/
Future<QStringList> FileSourceImporter::findWildcardMatches(const QUrl& urlPattern)
{
    OVITO_ASSERT(this_task::get());
    OVITO_ASSERT(isWildcardPattern(urlPattern));

    const QFileInfo fileInfo(urlPattern.path());
    const QString pattern = fileInfo.fileName();

    // Scan the directory for files matching the wildcard pattern.
    if(urlPattern.isLocalFile()) {
        QDir directory = QFileInfo(urlPattern.toLocalFile()).dir();

        // Search the full file list for matches and extract the numeric parts of the filenames.
        QStringList entries;
        for(const QString& filename : directory.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
            if(auto result = matchesWildcardPattern(pattern, filename); result.has_value())
                entries << result->toString();
        }

        // Numeric sorting of the entries.
        std::ranges::sort(entries, [](const QString& a, const QString& b) {
            return a.toLongLong() < b.toLongLong();
        });

        return Future<QStringList>::createImmediate(std::move(entries));
    }
    else {
        // Remove the filename part and keep the directory part of the URL.
        QUrl directoryUrl = urlPattern;
        directoryUrl.setPath(fileInfo.path());

        // Retrieve list of files in remote directory.
        return Application::instance()->fileManager().listDirectoryContents(directoryUrl).then([pattern](QStringList&& remoteFileList) {

            // Search the full file list for matches and extract the numeric parts of the filenames.
            QStringList entries;
            for(const QString& filename : remoteFileList) {
                if(auto result = matchesWildcardPattern(pattern, filename); result.has_value())
                    entries << result->toString();
            }

            // Numeric sorting of the entries.
            std::ranges::sort(entries, [](const QString& a, const QString& b) {
                return a.toLongLong() < b.toLongLong();
            });

            return entries;
        });
    }
}

/******************************************************************************
* Queries the filesystem and returns a list of files matching the given wildcard pattern.
* The filename pattern can contain exactly one '*' wildcard character, which represents a sequence of digits of arbitrary length.
* This version of the function returns the list of fully resolved file URLs.
******************************************************************************/
Future<std::vector<QUrl>> FileSourceImporter::findWildcardMatchesResolved(const QUrl& urlPattern)
{
    OVITO_ASSERT(this_task::get());

    // Determine whether the filename contains a wildcard character.
    if(!isWildcardPattern(urlPattern)) {
        // It's not a wildcard pattern. Return just a single file.
        return std::vector<QUrl>{ urlPattern };
    }

    // Call subroutine to obtain a list of matching entries.
    // Then resolve them into full file URLs.
    return findWildcardMatches(urlPattern).then([urlPattern](QStringList&& entries) {
        return FileSourceImporter::resolveWildcardMatches(urlPattern, entries);
    });
}

/******************************************************************************
* Given a URL pattern and a list of entries, resolves the wildcard matches into full file URLs.
******************************************************************************/
std::vector<QUrl> FileSourceImporter::resolveWildcardMatches(const QUrl& urlPattern, const QStringList& entries)
{
    const QString  pattern = urlPattern.path();
    qsizetype wildcardPos = pattern.indexOf(QChar('*'));
    OVITO_ASSERT(wildcardPos >= 0);
    const QStringView headPart = QStringView(pattern).first(wildcardPos);
    const QStringView tailPart = QStringView(pattern).mid(wildcardPos + 1);

    // Generate final list of URLs.
    std::vector<QUrl> urls;
    urls.reserve(entries.size());
    for(const QString& numericPart : entries) {
        QUrl url = urlPattern;
        url.setPath(headPart % numericPart % tailPart);
        urls.push_back(std::move(url));
    }

    return urls;
}

/******************************************************************************
* Checks if a filename matches to the given wildcard pattern.
* If yes, returns the numeric part extracted from the filename.
******************************************************************************/
std::optional<QStringView> FileSourceImporter::matchesWildcardPattern(const QStringView pattern, const QStringView filename)
{
    QStringView::const_iterator p = pattern.cbegin();
    QStringView::const_iterator f = filename.cbegin();
    QStringView numericPart;
    while(p != pattern.cend() && f != filename.cend()) {
        if(*p == QChar('*')) {
            if(!f->isDigit())
                return std::nullopt;
            size_t numBegin = std::distance(filename.cbegin(), f);
            do { ++f; }
            while(f != filename.cend() && f->isDigit());
            numericPart = filename.sliced(numBegin, std::distance(filename.cbegin(), f) - numBegin);
            ++p;
            continue;
        }
        else if(*p != *f)
            return std::nullopt;
        ++p;
        ++f;
    }
    if(p == pattern.cend() && f == filename.cend())
        return numericPart;
    return std::nullopt;
}

/******************************************************************************
* Writes an animation frame information record to a binary output stream.
******************************************************************************/
SaveStream& operator<<(SaveStream& stream, const FileSourceImporter::Frame& frame)
{
    stream.beginChunk(0x03);
    stream << frame.sourceFile << frame.byteOffset << frame.lineNumber << frame.lastModificationTime << frame.label << frame.parserData;
    stream.endChunk();
    return stream;
}

/******************************************************************************
* Reads an animation frame information record from a binary input stream.
******************************************************************************/
LoadStream& operator>>(LoadStream& stream, FileSourceImporter::Frame& frame)
{
    stream.expectChunk(0x03);

    stream >> frame.sourceFile >> frame.byteOffset >> frame.lineNumber >> frame.lastModificationTime;
    if(stream.formatVersion() >= 30014) {
        stream >> frame.label;
    }
    else {
        // For backward compatibility with OVITO 3.13.
        QString oldLabel;
        stream >> oldLabel;
        frame.label = AnimationFrameLabel::parse(oldLabel);
    }
    if(stream.formatVersion() >= 30010) {
        stream >> frame.parserData;
    }
    else {
        // For backward compatibility with OVITO 3.8.
        qint64 oldParserData;
        stream >> oldParserData;
        if(oldParserData != 0)
            frame.parserData.setValue(oldParserData);
    }

    stream.closeChunk();
    return stream;
}

}   // End of namespace
