#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>

#ifndef NULL
  #define NULL ((void *)0)
#endif

#define LUMP_ENTITIES     0
#define LUMP_PLANES       1
#define LUMP_VERTEXES     2
#define LUMP_VISIBILITY   3
#define LUMP_NODES        4
#define LUMP_TEXINFO      5
#define LUMP_FACES        6
#define LUMP_LIGHTING     7
#define LUMP_LEAFS        8
#define LUMP_LEAFFACES    9
#define LUMP_LEAFBRUSHES 10
#define LUMP_EDGES       11
#define LUMP_SURFEDGES   12
#define LUMP_MODELS      13
#define LUMP_BRUSHES     14
#define LUMP_BRUSHSIDES  15
#define LUMP_POP         16
#define LUMP_AREAS       17
#define LUMP_AREAPORTALS 18
#define HEADER_LUMPS     19

//============================================
// Basic BSP Structures
//============================================
typedef struct {
  int fileofs;
  int filelen;
} lump_t;

typedef struct {
  char string[4];
  long version;
  lump_t lumps[HEADER_LUMPS];
} header_t;
header_t header;

// LUMP_ENTITIES = 0
typedef struct {
  char dentdata[0x40000];
} entdata_t;

// LUMP_PLANES = 1
typedef struct {
  float normal[3];
  float dist;
  int   type;
} plane_t;

// LUMP_VERTEXES = 2
typedef struct {
  float point[3];
} vertex_t;

// LUMP_VISIBILITY = 3
typedef struct {
  int numclusters;
  int bitofs[8][2]; // bitofs[numclusters][2]
} vis_t;

// LUMP_NODES = 4
typedef struct {
  int planenum;
  int child[2]; // negative numbers are -(leafs+1), not nodes
  short mins[3];
  short maxs[3];
  unsigned short firstface;
  unsigned short numfaces;
} node_t;

// LUMP_TEXINFO = 5
typedef struct texinfo_s {
  float vecs[2][4];  // [s/t][xyz offset]
  int   flags;       // miptex flags + overrides
  int   value;       // light emission, etc
  char  texture[32]; // texture name (textures/*.wal)
  int   nexttexinfo; // for animations, -1 = end of chain
} texinfo_t;

// LUMP_FACES = 6
typedef struct {
  unsigned short planenum;
  short side;
  int   firstedge;
  short numedges;
  short texinfo;
  unsigned char styles[4];
  int   lightofs;    // start of [numstyles*surfsize] samples
} face_t;

// LUMP_LIGHTING = 7
typedef struct {
  unsigned char dlightdata[0x200000];
} lightdata_t;

// LUMP_LEAFS = 8
typedef struct {
  int   contents;
  short cluster;
  short area;
  short mins[3];
  short maxs[3];
  unsigned short firstleafface;
  unsigned short numleaffaces;
  unsigned short firstleafbrush;
  unsigned short numleafbrushes;
} leaf_t;

// LUMP_LEAFFACES = 9
typedef struct {
  unsigned short dleaffaces[65536];
} leaffaces_t;

// LUMP_LEAFBRUSHES = 10
typedef struct {
  unsigned short dleafbrushes[65536];
} leafbrushes_t;

// LUMP_EDGES = 11
typedef struct {
  unsigned short v[2]; // vertex numbers
} edge_t;

// LUMP_SURFEDGES = 12
typedef struct {
  int dsurfedges[256000];
} surfedges_t;

// LUMP_MODELS = 13
typedef struct {
  float mins[3];
  float maxs[3];
  float origin[3]; // for sounds or lights
  int   headnode;
  int   firstface;
  int   numfaces;  // submodels just draw faces without walking the bsp tree
} model_t;

// LUMP_BRUSHES = 14
typedef struct {
  int firstside;
  int numsides;
  int contents;
} brush_t;

// LUMP_BRUSHSIDES = 15
typedef struct {
  unsigned short planenum; // facing out of the leaf
  short texinfo;
} brushside_t;

// LUMP_POP = 16
typedef struct {
  unsigned char dpop[256];
} pop_t;

// LUMP_AREA = 17
typedef struct {
  int numareaportals;
  int firstareaportal;
} area_t;

// LUMP_AREAPORTALS = 18
typedef struct {
  int portalnum;
  int otherarea;
} areaportal_t;

//===================================
// BSP Map structure
//===================================
typedef struct {
  int            num_entdatas;
  entdata_t     *entdatas;    //  0=LUMP INDEX
  int            num_planes;
  plane_t       *planes;      //  1
  int            num_vertexs;
  vertex_t      *vertexs;     //  2
  int            num_viss;
  vis_t         *vis;         //  3
  int            num_nodes;
  node_t        *nodes;       //  4
  int            num_texinfos;
  texinfo_t     *texinfos;    //  5
  int            num_faces;
  face_t        *faces;       //  6
  int            num_lightdatas;
  lightdata_t   *lightdatas;  //  7
  int            num_leafs;
  leaf_t        *leafs;       //  8
  int            num_leaffaces;
  leaffaces_t   *leaffaces;   //  9
  int            num_leafbrushes;
  leafbrushes_t *leafbrushes; // 10
  int            num_edges;
  edge_t        *edges;       // 11
  int            num_surfedges;
  surfedges_t   *surfedges;   // 12
  int            num_models;
  model_t       *models;      // 13
  int            num_brushes;
  brush_t       *brushes;     // 14
  int            num_brushsides;
  brushside_t   *brushsides;  // 15
  int            num_pops;
  pop_t         *pops;        // 16
  int            num_areas;
  area_t        *areas;       // 17
  int            num_areaportals;
  areaportal_t  *areaportals; // 18
} bsp_t;

//==================================================
//==================================================
//==================================================
void *xmalloc(unsigned long size) {
void *xdata;
  xdata = malloc(size);
  if (!xdata){
    fprintf(stderr, "xmalloc: %s\n", strerror(errno));
    exit(1); }
  return xdata;
}

unsigned long getp;     // Location of pointer in buffer
unsigned long numbytes; // Size of buffer (in bytes)
unsigned char *buffer;  // Pointer to the buffered data.

//==================================================
// Move num bytes from location into address
//==================================================
void getmem(void *addr, unsigned long bytes) {
  memmove(addr, buffer+getp, bytes);
  getp += bytes;
}

//=====================================================
//========= ROUTINES FOR READING BSP STRUCTS ==========
//=====================================================

//=====================================================
// LUMP 0 - Read all BSP entdatas from buffer.
//=====================================================
static entdata_t *readentdatas(bsp_t *map) {
entdata_t *entdatas;
int i;

  // Set GET pointer to start of entdata_t data
  getp = header.lumps[LUMP_ENTITIES].fileofs;

  // How many entdatas chars are there?
  map->num_entdatas = header.lumps[LUMP_ENTITIES].filelen;

  printf("entdata count=%d\n",map->num_entdatas);

  if (map->num_entdatas <= 0) return NULL;

  // Allocate 1 entdata_t structure
  entdatas = (entdata_t *)xmalloc(sizeof(entdata_t));

  // Sequentially read entdata_t chars from buffer
  for(i=0; i < map->num_entdatas; i++)
    getmem((void *)&entdatas->dentdata[i],sizeof(entdatas->dentdata[i]));

  return entdatas;
}

//=====================================================
// LUMP 1 - Read all BSP planes from buffer.
//=====================================================
static plane_t *readplanes(bsp_t *map) {
plane_t *planes;
int i;

  // Set GET pointer to start of plane_t data
  getp = header.lumps[LUMP_PLANES].fileofs;

  // How many planes records are there?
  map->num_planes = header.lumps[LUMP_PLANES].filelen/sizeof(plane_t);

  printf("plane count=%d\n",map->num_planes);

  if (map->num_planes <= 0) return NULL;

  // Allocate all plane_t structures for num_planes
  planes = (plane_t *)xmalloc(map->num_planes*sizeof(plane_t));

  // Sequentially read plane_t structs from buffer
  for(i=0; i < map->num_planes; i++)
    getmem((void *)&planes[i],sizeof(plane_t));

  return planes;
}

//=====================================================
// LUMP 2 - Read all BSP vertexs from buffer.
//=====================================================
static vertex_t *readvertexs(bsp_t *map) {
vertex_t *vertexs;
int i;

  // Set GET pointer to start of vertex_t data
  getp = header.lumps[LUMP_VERTEXES].fileofs;

  // How many vertexs records are there?
  map->num_vertexs = header.lumps[LUMP_VERTEXES].filelen/sizeof(vertex_t);

  printf("vertex count=%d\n",map->num_vertexs);

  if (map->num_vertexs <= 0) return NULL;

  // Allocate all vertex_t structures for num_vertexs
  vertexs = (vertex_t *)xmalloc(map->num_vertexs*sizeof(vertex_t));

  // Sequentially read vertex_t structs from buffer
  for(i=0; i < map->num_vertexs; i++)
    getmem((void *)&vertexs[i],sizeof(vertex_t));

  return vertexs;
}

//=====================================================
// LUMP 3 - Read all BSP vis from buffer.
//=====================================================
static vis_t *readviss(bsp_t *map) {
vis_t *viss;
int i;

  // Set GET pointer to start of vis_t data
  getp = header.lumps[LUMP_VISIBILITY].fileofs;

  // How many vis_t records are there?
  map->num_viss = header.lumps[LUMP_VISIBILITY].filelen/sizeof(vis_t);

  printf("vis count=%d\n",map->num_viss);

  if (map->num_viss <= 0) return NULL;

  // Allocate all vis_t structures for num_viss
  viss = (vis_t *)xmalloc(map->num_viss*sizeof(vis_t));

  // Sequentially read vis_t structs from buffer
  for(i=0; i < map->num_viss; i++)
    getmem((void *)&viss[i],sizeof(vis_t));

  return viss;
}

//=====================================================
// LUMP 4 - Read all BSP nodes from buffer.
//=====================================================
static node_t *readnodes(bsp_t *map) {
node_t *nodes;
int i;

  // Set GET pointer to start of node_t data
  getp = header.lumps[LUMP_NODES].fileofs;

  // How many nodes records are there?
  map->num_nodes = header.lumps[LUMP_NODES].filelen/sizeof(node_t);

  printf("node count=%d\n",map->num_nodes);

  if (map->num_nodes <= 0) return NULL;

  // Allocate all node_t structures for num_nodes
  nodes = (node_t *)xmalloc(map->num_nodes*sizeof(node_t));

  // Sequentially read node_t structs from buffer
  for(i=0; i < map->num_nodes; i++)
    getmem((void *)&nodes[i],sizeof(node_t));

  return nodes;
}

//=====================================================
// LUMP 5 - Read all BSP texinfos from buffer.
//=====================================================
static texinfo_t *readtexinfos(bsp_t *map) {
texinfo_t *texinfos;
int i;

  // Set GET pointer to start of texinfo_t data
  getp = header.lumps[LUMP_TEXINFO].fileofs;

  // How many texinfos records are there?
  map->num_texinfos = header.lumps[LUMP_TEXINFO].filelen/sizeof(texinfo_t);

  printf("texinfo count=%d\n",map->num_texinfos);

  if (map->num_texinfos <= 0) return NULL;

  // Allocate all texinfo_t structures for num_texinfos
  texinfos = (texinfo_t *)xmalloc(map->num_texinfos*sizeof(texinfo_t));

  // Sequentially read texinfo_t structs from buffer
  for(i=0; i < map->num_texinfos; i++)
    getmem((void *)&texinfos[i],sizeof(texinfo_t));

  return texinfos;
}

//=====================================================
// LUMP 6 - Read all BSP faces from buffer.
//=====================================================
static face_t *readfaces(bsp_t *map) {
face_t *faces;
int i;

  // Set GET pointer to start of face_t data
  getp = header.lumps[LUMP_FACES].fileofs;

  // How many faces records are there?
  map->num_faces = header.lumps[LUMP_FACES].filelen/sizeof(face_t);

  printf("face count=%d\n",map->num_faces);

  if (map->num_faces <= 0) return NULL;

  // Allocate all face_t structures for num_faces
  faces = (face_t *)xmalloc(map->num_faces*sizeof(face_t));

  // Sequentially read face_t structs from buffer
  for(i=0; i < map->num_faces; i++)
    getmem((void *)&faces[i],sizeof(face_t));

  return faces;
}

//=====================================================
// LUMP 7 - Read all BSP lightdatas from buffer.
//=====================================================
static lightdata_t *readlightdatas(bsp_t *map) {
lightdata_t *lightdatas;
int i;

  // Set GET pointer to start of lightdata_t data
  getp = header.lumps[LUMP_LIGHTING].fileofs;

  // How many lightdatas chars are there?
  map->num_lightdatas = header.lumps[LUMP_LIGHTING].filelen;

  printf("lightdata count=%d\n",map->num_lightdatas);

  if (map->num_lightdatas <= 0) return NULL;

  // Allocate 1 lightdata structure
  lightdatas = (lightdata_t *)xmalloc(sizeof(lightdata_t));

  // Sequentially read lightdata bytes from buffer
  for(i=0; i < map->num_lightdatas; i++)
    getmem((void *)&lightdatas->dlightdata[i],sizeof(lightdatas->dlightdata[i]));

  return lightdatas;
}

//=====================================================
// LUMP 8 - Read all BSP leafs from buffer.
//=====================================================
static leaf_t *readleafs(bsp_t *map) {
leaf_t *leafs;
int i;

  // Set GET pointer to start of leaf_t data
  getp = header.lumps[LUMP_LEAFS].fileofs;

  // How many leafs records are there?
  map->num_leafs = header.lumps[LUMP_LEAFS].filelen/sizeof(leaf_t);

  printf("leaf count=%d\n",map->num_leafs);

  if (map->num_leafs <= 0) return NULL;

  // Allocate all leaf_t structures for num_leafs
  leafs = (leaf_t *)xmalloc(map->num_leafs*sizeof(leaf_t));

  // Sequentially read leaf_t structs from buffer
  for(i=0; i < map->num_leafs; i++)
    getmem((void *)&leafs[i],sizeof(leaf_t));

  return leafs;
}

//=====================================================
// LUMP 9 - Read all BSP leaffaces from buffer.
//=====================================================
static leaffaces_t *readleaffaces(bsp_t *map) {
leaffaces_t *leaffaces;
int i;

  // Set GET pointer to start of leafface_t data
  getp = header.lumps[LUMP_LEAFFACES].fileofs;

  // How many leaffaces are there?
  map->num_leaffaces = header.lumps[LUMP_LEAFFACES].filelen;

  printf("leafface count=%d\n",map->num_leaffaces);

  if (map->num_leaffaces <= 0) return NULL;

  // Allocate 1 leaffaces structure
  leaffaces = (leaffaces_t *)xmalloc(sizeof(leaffaces_t));

  // Sequentially read leafface bytes from buffer
  for(i=0; i < map->num_leaffaces; i++)
    getmem((void *)&leaffaces->dleaffaces[i],sizeof(leaffaces->dleaffaces[i]));

  return leaffaces;
}

//=====================================================
// LUMP 10 - Read all BSP leafbrushes from buffer.
//=====================================================
static leafbrushes_t *readleafbrushes(bsp_t *map) {
leafbrushes_t *leafbrushes;
int i;

  // Set GET pointer to start of leafbrushes_t data
  getp = header.lumps[LUMP_LEAFBRUSHES].fileofs;

  // How many leafbrushes are there?
  map->num_leafbrushes = header.lumps[LUMP_LEAFBRUSHES].filelen;

  printf("leafbrushes count=%d\n",map->num_leafbrushes);

  if (map->num_leafbrushes <= 0) return NULL;

  // Allocate 1 leafbrushes structure
  leafbrushes = (leafbrushes_t *)xmalloc(sizeof(leafbrushes_t));

  // Sequentially read leafbrushes bytes from buffer
  for(i=0; i < map->num_leafbrushes; i++)
    getmem((void *)&leafbrushes->dleafbrushes[i],sizeof(leafbrushes->dleafbrushes[i]));

  return leafbrushes;
}

//=====================================================
// LUMP 11 - Read all BSP edges from buffer.
//=====================================================
static edge_t *readedges(bsp_t *map) {
edge_t *edges;
int i;

  // Set GET pointer to start of edge_t data
  getp = header.lumps[LUMP_EDGES].fileofs;

  // How many edges records are there?
  map->num_edges = header.lumps[LUMP_EDGES].filelen/sizeof(edge_t);

  printf("edge count=%d\n",map->num_edges);

  if (map->num_edges <= 0) return NULL;

  // Allocate all edge_t structures for num_edges
  edges = (edge_t *)xmalloc(map->num_edges*sizeof(edge_t));

  // Sequentially read edge_t structs from buffer
  for(i=0; i < map->num_edges; i++)
    getmem((void *)&edges[i],sizeof(edge_t));

  return edges;
}

//=====================================================
// LUMP 12 - Read all BSP surfedges from buffer.
//=====================================================
static surfedges_t *readsurfedges(bsp_t *map) {
surfedges_t *surfedges;
int i;

  // Set GET pointer to start of surfedge_t data
  getp = header.lumps[LUMP_SURFEDGES].fileofs;

  // How many surfedges are there?
  map->num_surfedges = header.lumps[LUMP_SURFEDGES].filelen;

  printf("surfedges count=%d\n",map->num_surfedges);

  if (map->num_surfedges <= 0) return NULL;

  // Allocate 1 surfedges structure
  surfedges = (surfedges_t *)xmalloc(sizeof(surfedges_t));

  // Sequentially read surfedges bytes from buffer
  for(i=0; i < map->num_surfedges; i++)
    getmem((void *)&surfedges->dsurfedges[i],sizeof(surfedges->dsurfedges[i]));

  return surfedges;
}

//=====================================================
// LUMP 13 - Read all BSP models from buffer.
//=====================================================
static model_t *readmodels(bsp_t *map) {
model_t *models;
int i;

  // Set GET pointer to start of model_t data
  getp = header.lumps[LUMP_MODELS].fileofs;

  // How many models records are there?
  map->num_models = header.lumps[LUMP_MODELS].filelen/sizeof(model_t);

  printf("model count=%d\n",map->num_models);

  if (map->num_models <= 0) return NULL;

  // Allocate all model_t structures for num_models
  models = (model_t *)xmalloc(map->num_models*sizeof(model_t));

  // Sequentially read model_t structs from buffer
  for(i=0; i < map->num_models; i++)
    getmem((void *)&models[i],sizeof(model_t));

  return models;
}

//=====================================================
// LUMP 14 - Read all BSP brushes from buffer.
//=====================================================
static brush_t *readbrushes(bsp_t *map) {
brush_t *brushes;
int i;

  // Set GET pointer to start of surfedge_t data
  getp = header.lumps[LUMP_BRUSHES].fileofs;

  // How many brushes records are there?
  map->num_brushes = header.lumps[LUMP_BRUSHES].filelen/sizeof(brush_t);

  printf("brushes count=%d\n",map->num_brushes);

  if (map->num_brushes <= 0) return NULL;

  // Allocate all brush_t structures for num_brushes
  brushes = (brush_t *)xmalloc(map->num_brushes*sizeof(brush_t));

  // Sequentially read brush_t structs from buffer
  for(i=0; i < map->num_brushes; i++)
    getmem((void *)&brushes[i],sizeof(brush_t));

  return brushes;
}

//=====================================================
// LUMP 15 - Read all BSP brushside from buffer.
//=====================================================
static brushside_t *readbrushsides(bsp_t *map) {
brushside_t *brushsides;
int i;

  // Set GET pointer to start of surfedge_t data
  getp = header.lumps[LUMP_BRUSHSIDES].fileofs;

  // How many brushsides records are there?
  map->num_brushsides = header.lumps[LUMP_BRUSHSIDES].filelen/sizeof(brushside_t);

  printf("brushsides count=%d\n",map->num_brushsides);

  if (map->num_brushsides <= 0) return NULL;

  // Allocate all brushside_t structures for num_brushsides
  brushsides = (brushside_t *)xmalloc(map->num_brushsides*sizeof(brushside_t));

  // Sequentially read brushside_t structs from buffer
  for(i=0; i < map->num_brushsides; i++)
    getmem((void *)&brushsides[i],sizeof(brushside_t));

  return brushsides;
}

//=====================================================
// LUMP 16 - Read all BSP pops from buffer.
//=====================================================
static pop_t *readpops(bsp_t *map) {
pop_t *pops;
int i;

  // Set GET pointer to start of surfedge_t data
  getp = header.lumps[LUMP_POP].fileofs;

  // How many pops chars are there?
  map->num_pops = header.lumps[LUMP_POP].filelen;

  printf("pops count=%d\n",map->num_pops);

  if (map->num_pops <= 0) return NULL;

  // Allocate 1 pop structure
  pops = (pop_t *)xmalloc(sizeof(pop_t));

  // Sequentially read pop bytes from buffer
  for(i=0; i < map->num_pops; i++)
    getmem((void *)&pops->dpop[i],sizeof(pops->dpop[i]));

  return pops;
}

//=====================================================
// LUMP 17 - Read all BSP areas from buffer.
//=====================================================
static area_t *readareas(bsp_t *map) {
area_t *areas;
int i;

  // Set GET pointer to start of surfedge_t data
  getp = header.lumps[LUMP_AREAS].fileofs;

  // How many areas records are there?
  map->num_areas = header.lumps[LUMP_AREAS].filelen/sizeof(area_t);

  printf("areas count=%d\n",map->num_areas);

  if (map->num_areas <= 0) return NULL;

  // Allocate all area_t structures for num_areas
  areas = (area_t *)xmalloc(map->num_areas*sizeof(area_t));

  // Sequentially read area_t structs from buffer
  for(i=0; i < map->num_areas; i++)
    getmem((void *)&areas[i],sizeof(area_t));

  return areas;
}

//=====================================================
// LUMP 18 - Read all BSP areaportals from buffer.
//=====================================================
static areaportal_t *readareaportals(bsp_t *map) {
areaportal_t *areaportals;
int i;

  // Set GET pointer to start of surfedge_t data
  getp = header.lumps[LUMP_AREAPORTALS].fileofs;

  // How many areaportal records are there?
  map->num_areaportals = header.lumps[LUMP_AREAPORTALS].filelen/sizeof(areaportal_t);

  printf("areaportals count=%d\n",map->num_areaportals);

  if (map->num_areaportals <= 0) return NULL;

  // Allocate all areaportal_t structures for num_areaportals
  areaportals = (areaportal_t *)xmalloc(map->num_areaportals*sizeof(areaportal_t));

  // Sequentially read areaportal_t structs from buffer
  for(i=0; i < map->num_areaportals; i++)
    getmem((void *)&areaportals[i],sizeof(areaportal_t));

  return areaportals;
}

//================================================
// Reads entire BSP file into bsp_t struct.
//================================================
bsp_t *load_bsp_map(void) {
bsp_t *map;

  // Read file header
  getmem((void*)&header,sizeof(header_t));

  // Allocate bsp_t struct
  map = (bsp_t *)xmalloc(sizeof(bsp_t));

  // Load up entire map. Order not important.
  map->entdatas = readentdatas(map);
  map->planes = readplanes(map);
  map->vertexs = readvertexs(map);
  map->vis = readviss(map);
  map->nodes  = readnodes(map);
  map->texinfos = readtexinfos(map);
  map->faces  = readfaces(map);
  map->lightdatas = readlightdatas(map);
  map->leafs  = readleafs(map);
  map->leaffaces = readleaffaces(map);
  map->leafbrushes = readleafbrushes(map);
  map->edges = readedges(map);
  map->surfedges =readsurfedges(map);
  map->models = readmodels(map);
  map->brushes = readbrushes(map);
  map->brushsides = readbrushsides(map);
  map->pops = readpops(map);
  map->areas = readareas(map);
  map->areaportals = readareaportals(map);

  return map;
}

//=================================================
// Open BSP file at filepath and read buffer_t.
//================================================
bsp_t *loadbsp(char *filepath) {
FILE *f;
long bytesread;

  printf("\n\n%s\n",filepath);

  // Open filepath for read-only binary..
  f = fopen(filepath, "rb");
  if (!f || errno) {
    fprintf(stderr, "fopen: %s\n", strerror(errno));
    fclose(f);
    return NULL; }

  // Move f pointer to EOF
  fseek(f, 0, SEEK_END);

  // How many bytes in this file?
  numbytes = ftell(f);

  // Allocate numbytes for buffer
  buffer = xmalloc(numbytes);

  // Reset f to start of file
  fseek(f, 0, SEEK_SET);

  // Read ALL bytes from BSP file into buffer.
  // All operations done from buffer, not file.
  // fread() extremely fast if reading entire file at once.
  bytesread = fread(buffer, 1, numbytes, f);

  // ALL bytes read?
  if (bytesread != numbytes || errno) {
    fprintf(stderr, "fread: %s\n", strerror(errno));
    free(buffer);
    fclose(f);
    return NULL; }

  // Don't need file pointer any longer.
  fclose(f);

  return load_bsp_map();
}

//================================================
// Release the BSP map from memory..
//================================================
void bsp_free(bsp_t *map) {
  free(map->leafs);
  free(map->nodes);
  free(map->planes);
  free(map);
}

//=================================================
int main(int argc, char *argv[]) {
char t;
bsp_t *map;

  map = loadbsp("c:\\quake2\\baseq2\\maps\\chaosdm1.bsp");

  printf("\n\nWaiting for input  ");
  t=getchar();

  bsp_free(map);

  return 0;
}