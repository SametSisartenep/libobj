/* vertex types */
enum {
	OBJVGeometric,
	OBJVTexture,
	OBJVNormal,
	OBJVParametric,
	OBJNVERT
};

/* element types */
enum {
	OBJEPoint,
	OBJELine,
	OBJEFace,
	OBJECurve,
	OBJECurve2,
	OBJESurface
};

/* grouping types */
enum {
	OBJGGlobal,
	OBJGSmoothing,
	OBJGMerging
};

/* object hash table size */
enum {
	OBJHTSIZE = 17
};

typedef union OBJVertex OBJVertex;
typedef struct OBJColor OBJColor;
typedef struct OBJTexture OBJTexture;
typedef struct OBJVertexArray OBJVertexArray;
typedef struct OBJIndexArray OBJIndexArray;
typedef struct OBJMaterial OBJMaterial;
typedef struct OBJMaterlist OBJMaterlist;
typedef struct OBJElem OBJElem;
//typedef struct OBJGroup OBJGroup;
typedef struct OBJObject OBJObject;
typedef struct OBJ OBJ;

#pragma varargck type "O" OBJ*
#pragma varargck type "M" OBJMaterlist*

union OBJVertex
{
	struct { double x, y, z, w; };	/* geometric */
	struct { double u, v, vv; };	/* texture and parametric */
	struct { double i, j, k; };	/* normal */
};

struct OBJColor
{
	double r, g, b, a;
};

struct OBJTexture
{
	char *filename;
	Memimage *image;
};

struct OBJVertexArray
{
	OBJVertex *verts;
	int nvert;
};

struct OBJIndexArray
{
	int *indices;
	int nindex;
};

struct OBJMaterial
{
	char *name;
	OBJColor Ka;		/* ambient color */
	OBJColor Kd;		/* diffuse color */
	OBJColor Ks;		/* specular color */
	OBJColor Ke;		/* emissive color */
	double Ns;		/* specular highlight */
	double Ni;		/* index of refraction */
	double d;		/* dissolution factor (opacity) */
	int illum;		/* illumination model */
	OBJTexture *map_Kd;	/* color texture file */
	OBJTexture *map_Ks;	/* specular texture file */
	OBJTexture *norm;	/* normal texture file */
	OBJMaterial *next;
};

struct OBJMaterlist
{
	char *filename;
	OBJMaterial *mattab[OBJHTSIZE];
};

struct OBJElem
{
	OBJIndexArray indextab[OBJNVERT];
	int type;
	OBJMaterial *mtl;
	OBJElem *next;
};

//struct OBJGroup
//{
//	char *name;
//	int type;
//	OBJElem *elem0;
//	OBJGroup *next;
//};
//struct OBJObject
//{
//	char *name;
//	OBJGroup *grptab[OBJHTSIZE];
//	OBJObject *next;
//};

struct OBJObject
{
	char *name;
	OBJElem *child;
	OBJElem *lastone;
	OBJObject *next;
};

struct OBJ
{
	OBJVertexArray vertdata[OBJNVERT];
	OBJObject *objtab[OBJHTSIZE];
	OBJMaterlist *materials;
};

void objaddvertex(OBJ*, OBJVertex, int);
void objaddelem(OBJObject*, OBJElem*);
OBJElem *objallocelem(int);
void objaddelemidx(OBJElem*, int, int);
void objfreeelem(OBJElem*);
OBJObject *objallocobject(char*);
void objfreeobject(OBJObject*);
void objpushobject(OBJ*, OBJObject*);
OBJObject *objgetobject(OBJ*, char*);
void objfreetexture(OBJTexture*);
OBJMaterial *objallocmt(char*);
void objfreemt(OBJMaterial*);
OBJMaterlist *objallocmtl(char*);
void objaddmtl(OBJMaterlist*, OBJMaterial*);
OBJMaterial *objgetmtl(OBJMaterlist*, char*);
OBJ *objparse(char*);
void objfree(OBJ*);
OBJMaterlist *objmtlparse(char*);
void objmtlfree(OBJMaterlist*);
int objexport(char*, OBJ*);

int OBJMaterlistfmt(Fmt*);
int OBJfmt(Fmt*);
void OBJfmtinstall(void);
