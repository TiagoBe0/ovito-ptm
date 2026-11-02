import sys
L=(40.0,30.0,20.0)
lines=[
 ((0.5,0.0,0.5), [(-6.0,10.0,4.0),(3.0,10.5,6.0),(12.0,11.0,9.0),(47.0,12.0,15.0)]),   # crosses x boundary twice
 ((0.0,0.5,-0.5),[(5.0,3.0,1.0),(30.0,25.0,18.0)]),                                     # 3D diagonal
 ((0.5,0.5,0.0), [(20.0,15.0,10.0),(26.0,15.0,10.0),(26.0,21.0,12.0),(20.0,21.0,10.0),(20.0,15.0,10.0)]), # loop
]
o=["CA_FILE_VERSION 6","CA_LIB_VERSION 0.0.0","STRUCTURE_TYPES 1","STRUCTURE_TYPE 1","NAME fcc","FULL_NAME FCC","COLOR 0.4 1.0 0.4","TYPE LATTICE",
   "BURGERS_VECTOR_FAMILIES 1","BURGERS_VECTOR_FAMILY ID 0","Other","0 0 0","0.9 0.9 0.9","END_STRUCTURE_TYPE",
   "SIMULATION_CELL_ORIGIN 0.0 0.0 0.0","SIMULATION_CELL_MATRIX","%g 0 0"%L[0],"0 %g 0"%L[1],"0 0 %g"%L[2],"PBC_FLAGS 1 1 1",
   "CLUSTERS 1","CLUSTER 1","CLUSTER_STRUCTURE 1","CLUSTER_ORIENTATION",
   "2.8 -2.9 0.1","2.4 2.3 -2.3","1.6 1.7 3.3",   # rows of the orientation matrix
   "CLUSTER_COLOR 1 1 1","CLUSTER_SIZE 100","END_CLUSTER","CLUSTER_TRANSITIONS 0",
   "DISLOCATIONS %d"%len(lines)]
for i,(b,pts) in enumerate(lines):
    o+= [str(i), "%g %g %g"%b, "1", str(len(pts))] + ["%g %g %g"%p for p in pts]
o+=["DISLOCATION_JUNCTIONS"]+[s for i in range(len(lines)) for s in ("0 %d"%i,"1 %d"%i)]
o+=["DEFECT_MESH_VERTICES 0","DEFECT_MESH_FACETS 0"]
open(sys.argv[1],"w").write("\n".join(o)+"\n")
