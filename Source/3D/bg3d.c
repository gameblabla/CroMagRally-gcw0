/****************************/
/*   	BG3D.C 				*/
/* (c)1999 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "globals.h"
#include "misc.h"
#include "bg3d.h"
#include "metaobjects.h"
#include "window.h"
#include "skeletonobj.h"
#include "mobjtypes.h"


/****************************/
/*    PROTOTYPES            */
/****************************/

static void ReadBG3DHeader(short refNum);
static void ParseBG3DFile(short refNum);
static void ReadMaterialFlags(short refNum);
static void ReadMaterialDiffuseColor(short refNum);
static void ReadMaterialTextureMap(short refNum);
static void ReadGroup(void);
static MetaObjectPtr ReadNewGeometry(short refNum);
static MetaObjectPtr ReadVertexElementsGeometry(BG3DGeometryHeader *header);
static void InitBG3DContainer(void);
static void EndGroup(void);
static void ReadVertexArray(short refNum);
static void ReadNormalArray(short refNum);
static void ReadUVArray(short refNum);
static void ReadVertexColorArray(short refNum);
static void ReadTriangleArray(short refNum);
static void PreLoadTextureMaterials(void);


/****************************/
/*    CONSTANTS             */
/****************************/

#define	BG3D_GROUP_STACK_SIZE	50						// max nesting depth of groups



/*********************/
/*    VARIABLES      */
/*********************/

//static OGLSetupOutputType	*gBG3D_CurrentDrawContext = nil;
static int					gBG3D_GroupStackIndex;
static MOGroupObject		*gBG3D_GroupStack[BG3D_GROUP_STACK_SIZE];
static MOGroupObject		*gBG3D_CurrentGroup;

static MOMaterialObject		*gBG3D_CurrentMaterialObj;			// note: this variable contains an illegal ref to the object.  The real ref is in the file container material list.
static MOVertexArrayObject	*gBG3D_CurrentGeometryObj;

static BG3DFileContainer	*gBG3D_CurrentContainer;


BG3DFileContainer		*gBG3DContainerList[MAX_BG3D_GROUPS];
MetaObjectPtr			gBG3DGroupList[MAX_BG3D_GROUPS][MAX_OBJECTS_IN_GROUP];		// ILLEGAL references!!!
int						gNumObjectsInBG3DGroupList[MAX_BG3D_GROUPS];
OGLBoundingBox			gObjectGroupBBoxList[MAX_BG3D_GROUPS][MAX_OBJECTS_IN_GROUP];


/****************** INIT BG3D MANAGER ***********************/

void InitBG3DManager(void)
{
int	i;

	for (i = 0; i < MAX_BG3D_GROUPS; i++)
	{
		gBG3DContainerList[i] = nil;
	}
}


/********************** IMPORT BG3D **************************/
//
// NOTE:  	All BG3D models must be imported AFTER the draw context has been created,
//			because all imported textures are named with OpenGL and loaded into OpenGL!
//

void ImportBG3D(FSSpec *spec, int groupNum)
{
short				refNum;
int					i;
MetaObjectHeader	*header;
MOGroupObject		*group;
MOGroupData			*data;

			/* INIT SOME VARIABLES */

//	gBG3D_CurrentDrawContext	= setupInfo;
	gBG3D_CurrentMaterialObj 	= nil;
	gBG3D_CurrentGeometryObj	= nil;
	gBG3D_GroupStackIndex		= 0;			// init the group stack
	InitBG3DContainer();


		/************************/
		/* OPEN THE FILE & READ */
		/************************/

	if (FSpOpenDF(spec, fsRdPerm, &refNum) != noErr)
		DoFatalAlert("ImportBG3D: FSpOpenDF failed");

	ReadBG3DHeader(refNum);
	ParseBG3DFile(refNum);

			/* CLOSE FILE */

	FSClose(refNum);


		/*********************************************/
		/* PRELOAD ALL TEXTURE MATERIALS INTO OPENGL */
		/*********************************************/

	PreLoadTextureMaterials();


			/********************/
			/* SETUP GROUP INFO */
			/********************/
			//
			// A model "group" is a grouping of 3D models.
			//

	gBG3DContainerList[groupNum] = gBG3D_CurrentContainer;			// save container into list

	header = gBG3D_CurrentContainer->root;							// point to root object in container
	if (header == nil)
		DoFatalAlert("ImportBG3D: header == nil");

	if (header->type != MO_TYPE_GROUP)								// root should be a group
		DoFatalAlert("ImportBG3D: root isnt a group!");


			/* PARSE GROUP */
			//
			// Create a list of all of the models inside this file & Calc bounding box
			//

	group = gBG3D_CurrentContainer->root;
	data = &group->objectData;

	for (i = 0; i < data->numObjectsInGroup; i++)
	{
		OGLBoundingBox	*bbox = &gObjectGroupBBoxList[groupNum][i];

		gBG3DGroupList[groupNum][i] = data->groupContents[i];			// copy ILLEGAL ref to this object
		MO_CalcBoundingBox(data->groupContents[i], bbox);				// calc bounding box of this model
	}

	gNumObjectsInBG3DGroupList[groupNum] = data->numObjectsInGroup;
}


/********************** READ BG3D HEADER **************************/

static void ReadBG3DHeader(short refNum)
{
BG3DHeaderType	headerData;
long			count;


	count = sizeof(BG3DHeaderType);

	if (FSRead(refNum, &count, (Ptr) &headerData) != noErr)
		DoFatalAlert("ReadBG3DHeader: FSRead failed");

			/* VERIFY FILE */

	if ((headerData.headerString[0] != 'B') ||
		(headerData.headerString[1] != 'G') ||
		(headerData.headerString[2] != '3') ||
		(headerData.headerString[3] != 'D'))
	{
		DoFatalAlert("ReadBG3DHeader: BG3D file has invalid header.");
	}
}


/****************** PARSE BG3D FILE ***********************/

static void ParseBG3DFile(short refNum)
{
uint32_t		tag;
long			count;
Boolean			done = false;
MetaObjectPtr 	newObj;

	do
	{
			/* READ A TAG */

		count = sizeof(tag);
		if (FSRead(refNum, &count, (Ptr) &tag) != noErr)
			DoFatalAlert("ParseBG3DFile: FSRead failed");
		tag = UnpackU32BE(&tag);


			/* HANDLE THE TAG */

		switch(tag)
		{
			case	BG3D_TAGTYPE_MATERIALFLAGS:
					ReadMaterialFlags(refNum);
					break;

			case	BG3D_TAGTYPE_MATERIALDIFFUSECOLOR:
					ReadMaterialDiffuseColor(refNum);
					break;

			case	BG3D_TAGTYPE_TEXTUREMAP:
					ReadMaterialTextureMap(refNum);
					break;

			case	BG3D_TAGTYPE_GROUPSTART:
					ReadGroup();
					break;

			case	BG3D_TAGTYPE_GROUPEND:
					EndGroup();
					break;

			case	BG3D_TAGTYPE_GEOMETRY:
					newObj = ReadNewGeometry(refNum);
					if (gBG3D_CurrentGroup)								// add new geometry to current group
					{
						MO_AppendToGroup(gBG3D_CurrentGroup, newObj);
						MO_DisposeObjectReference(newObj);								// nuke the extra reference
					}
					break;

			case	BG3D_TAGTYPE_VERTEXARRAY:
					ReadVertexArray(refNum);
					break;

			case	BG3D_TAGTYPE_NORMALARRAY:
					ReadNormalArray(refNum);
					break;

			case	BG3D_TAGTYPE_UVARRAY:
					ReadUVArray(refNum);
					break;

			case	BG3D_TAGTYPE_COLORARRAY:
					ReadVertexColorArray(refNum);
					break;

			case	BG3D_TAGTYPE_TRIANGLEARRAY:
					ReadTriangleArray(refNum);
					break;

			case	BG3D_TAGTYPE_ENDFILE:
					done = true;
					break;

			default:
					DoFatalAlert("ParseBG3DFile: unrecognized tag");
		}
	}while(!done);
}


/******************* READ MATERIAL FLAGS ***********************/
//
// Reading new material flags indicatest the start of a new material.
//

static void ReadMaterialFlags(short refNum)
{
long				count,i;
MOMaterialData		data;
uint32_t			flags;

			/* READ FLAGS */

	count = sizeof(flags);
	if (FSRead(refNum, &count, (Ptr) &flags) != noErr)
		DoFatalAlert("ReadMaterialFlags: FSRead failed");
	flags = UnpackU32BE(&flags);


		/* INIT NEW MATERIAL DATA */

//	data.setupInfo		= gBG3D_CurrentDrawContext;		// remember which draw context this material is assigned to
	data.flags 			= flags;
	data.diffuseColor.r	= 1;
	data.diffuseColor.g	= 1;
	data.diffuseColor.b	= 1;
	data.diffuseColor.a	= 1;
	data.numMipmaps		= 0;				// there are currently 0 textures assigned to this material

	/* CREATE NEW MATERIAL OBJECT */

	gBG3D_CurrentMaterialObj = MO_CreateNewObjectOfType(MO_TYPE_MATERIAL, 0, &data);


	/* ADD THIS MATERIAL TO THE FILE CONTAINER */


	i = gBG3D_CurrentContainer->numMaterials++;									// get index into file container's material list

	gBG3D_CurrentContainer->materials[i] = gBG3D_CurrentMaterialObj;			// stores the 1 reference here.
}


/*************** READ MATERIAL DIFFUSE COLOR **********************/

static void ReadMaterialDiffuseColor(short refNum)
{
long			count;
GLfloat			color[4];
MOMaterialData	*data;

	if (gBG3D_CurrentMaterialObj == nil)
		DoFatalAlert("ReadMaterialDiffuseColor: gBG3D_CurrentMaterialObj == nil");


			/* READ COLOR VALUE */

	count = sizeof(GLfloat) * 4;
	if (FSRead(refNum, &count, (Ptr) color) != noErr)
		DoFatalAlert("ReadMaterialDiffuseColor: FSRead failed");
	UnpackStructs(">4f", sizeof(color), 1, color);


		/* ASSIGN COLOR TO CURRENT MATERIAL */

	data = &gBG3D_CurrentMaterialObj->objectData; 							// get ptr to material data

	data->diffuseColor.r = color[0];
	data->diffuseColor.g = color[1];
	data->diffuseColor.b = color[2];
	data->diffuseColor.a = color[3];
}


/******************* READ MATERIAL TEXTURE MAP ***********************/
//
// NOTE:  This may get called multiple times - once for each mipmap associated with the
//			material.
//

static void ReadMaterialTextureMap(short refNum)
{
BG3DTextureHeader	textureHeader;
long		count,i;
void		*texturePixels;
MOMaterialData	*data;

			/* GET PTR TO CURRENT MATERIAL */

	if (gBG3D_CurrentMaterialObj == nil)
		DoFatalAlert("ReadMaterialTextureMap: gBG3D_CurrentMaterialObj == nil");

	data = &gBG3D_CurrentMaterialObj->objectData; 	// get ptr to material data


			/***********************/
			/* READ TEXTURE HEADER */
			/***********************/

	count = sizeof(BG3DTextureHeader);
	FSRead(refNum, &count, (Ptr) &textureHeader);		// read header
	UnpackStructs(">LLiiL4L", sizeof(textureHeader), 1, &textureHeader);

			/* COPY BASIC INFO */

	if (data->numMipmaps == 0)					// see if this is the first texture
	{
		data->width 			= textureHeader.width;
		data->height	 		= textureHeader.height;
		data->pixelSrcFormat 	= textureHeader.srcPixelFormat;		// internal format
		data->pixelDstFormat 	= textureHeader.dstPixelFormat;		// vram format
	}
	else
	if (data->numMipmaps >= MO_MAX_MIPMAPS)		// see if overflow
		DoFatalAlert("ReadMaterialTextureMap: mipmap overflow!");


		/***************************/
		/* READ THE TEXTURE PIXELS */
		/***************************/

	count = textureHeader.bufferSize;			// get size of buffer to load

	texturePixels = AllocPtr(count);			// alloc memory for buffer
	if (texturePixels == nil)
		DoFatalAlert("ReadMaterialTextureMap: AllocPtr failed");

	FSRead(refNum, &count, (Ptr) texturePixels);		// read pixel data


		/* ASSIGN PIXELS TO CURRENT MATERIAL */

	i = data->numMipmaps++;						// increment the mipmap count
	data->texturePixels[i] = texturePixels;		// set ptr to pixelmap

}


/******************* READ GROUP ***********************/
//
// Called when GROUPSTART tag is found.  There must be a matching GROUPEND tag later.
//

static void ReadGroup(void)
{
MOGroupObject	*newGroup;

			/* CREATE NEW GROUP OBJECT */

	newGroup = MO_CreateNewObjectOfType(MO_TYPE_GROUP, 0, nil);
	if (newGroup == nil)
		DoFatalAlert("ReadGroup: MO_CreateNewObjectOfType failed");


		/*************************/
		/* PUSH ONTO GROUP STACK */
		/*************************/

	if (gBG3D_GroupStackIndex >= (BG3D_GROUP_STACK_SIZE-1))
		DoFatalAlert("ReadGroup: gBG3D_GroupStackIndex overflow!");

		/* SEE IF THIS IS FIRST GROUP */

	if (gBG3D_CurrentGroup == nil)					// no parent
	{
		gBG3D_CurrentContainer->root = newGroup;	// set container's root to this group
	}

		/* ADD TO PARENT GROUP */

	else
	{
		gBG3D_GroupStack[gBG3D_GroupStackIndex++] = gBG3D_CurrentGroup;		// push the old group onto group stack
		MO_AppendToGroup(gBG3D_CurrentGroup, newGroup);						// add new group to existing group (which creates new ref)
		MO_DisposeObjectReference(newGroup);								// nuke the extra reference
	}

	gBG3D_CurrentGroup = newGroup;				// current group == this group
}


/******************* END GROUP ***********************/
//
// Signifies the end of a GROUPSTART tag group.
//

static void EndGroup(void)
{
	gBG3D_GroupStackIndex--;

	if (gBG3D_GroupStackIndex < 0)									// must be something on group stack
		DoFatalAlert("EndGroup: stack is empty!");

	gBG3D_CurrentGroup = gBG3D_GroupStack[gBG3D_GroupStackIndex++]; // get previous group off of stack
}

#pragma mark -

/******************* READ NEW GEOMETRY ***********************/

static MetaObjectPtr ReadNewGeometry(short refNum)
{
BG3DGeometryHeader	geoHeader;
long				count;
MetaObjectPtr		newObj;

			/* READ GEOMETRY HEADER */

	count = sizeof(BG3DGeometryHeader);
	FSRead(refNum, &count, (Ptr) &geoHeader);		// read header
	UnpackStructs(">Li4LLLL4L", sizeof(geoHeader), 1, &geoHeader);


		/******************************/
		/* CREATE NEW GEOMETRY OBJECT */
		/******************************/

	switch(geoHeader.type)
	{
				/* VERTEX ELEMENTS */

		case	BG3D_GEOMETRYTYPE_VERTEXELEMENTS:
				newObj = ReadVertexElementsGeometry(&geoHeader);
				break;

		default:
				DoFatalAlert("ReadNewGeometry: unknown geo type");
	}

	return(newObj);
}


/*************** READ VERTEX ELEMENTS GEOMETRY *******************/
//
//
//

static MetaObjectPtr ReadVertexElementsGeometry(BG3DGeometryHeader *header)
{
MOVertexArrayData vertexArrayData;
int		i;

			/* SETUP DATA */

	vertexArrayData.numMaterials 	= header->numMaterials;
	vertexArrayData.numPoints 		= header->numPoints;
	vertexArrayData.numTriangles 	= header->numTriangles;
	vertexArrayData.points 			= nil;							// these arrays havnt been read in yet
	vertexArrayData.normals 		= nil;
	vertexArrayData.uvs		 		= nil;
	vertexArrayData.colorsByte 		= nil;
	vertexArrayData.colorsFloat		= nil;
	vertexArrayData.triangles		= nil;

	/* SETUP MATERIAL LIST */
	//
	// These start as illegal references.  The ref count is incremented during the Object Creation function.
	//

	for (i = 0; i < vertexArrayData.numMaterials; i++)
	{
		int	materialNum = header->layerMaterialNum[i];

		vertexArrayData.materials[i] = gBG3D_CurrentContainer->materials[materialNum];
	}


		/* CREATE THE NEW GEO OBJECT */

	gBG3D_CurrentGeometryObj = MO_CreateNewObjectOfType(MO_TYPE_GEOMETRY, MO_GEOMETRY_SUBTYPE_VERTEXARRAY, &vertexArrayData);

	return(gBG3D_CurrentGeometryObj);
}


/******************* READ VERTEX ARRAY *************************/

static void ReadVertexArray(short refNum)
{
long				count;
int					numPoints;
MOVertexArrayData	*data;
OGLPoint3D			*pointList;

	data = &gBG3D_CurrentGeometryObj->objectData;					// point to geometry data
	if (data->points)												// see if points already assigned
		DoFatalAlert("ReadVertexArray: points already assigned!");

	numPoints = data->numPoints;									// get # points to expect to read

	count = sizeof(OGLPoint3D) * numPoints;							// calc size of data to read
	pointList = AllocPtr(count);									// alloc buffer to hold points
	if (pointList == nil)
		DoFatalAlert("ReadVertexArray: AllocPtr failed!");

	FSRead(refNum, &count, (Ptr) pointList);								// read the data
	UnpackStructs(">fff", sizeof(OGLPoint3D), numPoints, pointList);

	data->points = pointList;										// assign point array to geometry header
}


/******************* READ NORMAL ARRAY *************************/

static void ReadNormalArray(short refNum)
{
long				count;
int					numPoints;
MOVertexArrayData	*data;
OGLVector3D			*normalList;

	data = &gBG3D_CurrentGeometryObj->objectData;					// point to geometry data
	numPoints = data->numPoints;									// get # normals to expect to read

	count = sizeof(OGLVector3D) * numPoints;						// calc size of data to read
	normalList = AllocPtr(count);									// alloc buffer to hold normals
	if (normalList == nil)
		DoFatalAlert("ReadNormalArray: AllocPtr failed!");

	FSRead(refNum, &count, (Ptr) normalList);								// read the data
	UnpackStructs(">fff", sizeof(OGLVector3D), numPoints, normalList);

	data->normals = normalList;										// assign normal array to geometry header
}


/******************* READ UV ARRAY *************************/

static void ReadUVArray(short refNum)
{
long				count;
int					numPoints;
MOVertexArrayData	*data;
OGLTextureCoord		*uvList;

	data = &gBG3D_CurrentGeometryObj->objectData;					// point to geometry data
	numPoints = data->numPoints;									// get # uv's to expect to read

	count = sizeof(OGLTextureCoord) * numPoints;					// calc size of data to read
	uvList = AllocPtr(count);										// alloc buffer to hold uv's
	if (uvList == nil)
		DoFatalAlert("ReadUVArray: AllocPtr failed!");

	FSRead(refNum, &count, (Ptr) uvList);									// read the data
	UnpackStructs(">ff", sizeof(OGLTextureCoord), numPoints, uvList);

	data->uvs = uvList;												// assign uv array to geometry header
}


/******************* READ VERTEX COLOR ARRAY *************************/

static void ReadVertexColorArray(short refNum)
{
long				count,i;
int					numPoints;
MOVertexArrayData	*data;
OGLColorRGBA_Byte	*colorList;
OGLColorRGBA		*colorsF;

	data = &gBG3D_CurrentGeometryObj->objectData;					// point to geometry data
	numPoints = data->numPoints;									// get # colors to expect to read

	count = sizeof(OGLColorRGBA_Byte) * numPoints;					// calc size of data to read
	colorList = AllocPtr(count);									// alloc buffer to hold data
	if (colorList == nil)
		DoFatalAlert("ReadVertexColorArray: AllocPtr failed!");

	FSRead(refNum, &count, (Ptr) colorList);								// read the data
	// No need to byteswap this

	data->colorsByte = colorList;									// assign color array to geometry header

		/* NOW ALSO CREATE COLOR ARRAY IN FLOAT FORMAT */
		//
		// Models have colors in both byte and float formats because
		// it is faster to render with Bytes if no lighting, but faster with floats if doing lights
		//

	colorsF = AllocPtr(sizeof(OGLColorRGBA) * numPoints);
	if (colorsF == nil)
		DoFatalAlert("ReadVertexColorArray: AllocPtr failed!");

	data->colorsFloat = colorsF;									// assign color array to geometry header

	for (i = 0; i < numPoints; i++)									// copy & convert bytes to floats
	{
		colorsF[i].r = (float)(colorList[i].r) / 255.0f;
		colorsF[i].g = (float)(colorList[i].g) / 255.0f;
		colorsF[i].b = (float)(colorList[i].b) / 255.0f;
		colorsF[i].a = (float)(colorList[i].a) / 255.0f;
	}

}


/******************* READ TRIANGLE ARRAY *************************/

static void ReadTriangleArray(short refNum)
{
long				count;
int					numTriangles;
MOVertexArrayData	*data;
MOTriangleIndecies	*triList;

	data = &gBG3D_CurrentGeometryObj->objectData;					// point to geometry data
	numTriangles = data->numTriangles;								// get # triangles expect to read

	count = sizeof(MOTriangleIndecies) * numTriangles;				// calc size of data to read
	triList = AllocPtr(count);										// alloc buffer to hold data
	if (triList == nil)
		DoFatalAlert("ReadTriangleArray: AllocPtr failed!");

	FSRead(refNum, &count, (Ptr) triList);								// read the data
	UnpackStructs(">LLL", sizeof(MOTriangleIndecies), numTriangles, triList);

	data->triangles = triList;										// assign triangle array to geometry header
}







#pragma mark -


/***************** INIT BG3D CONTAINER *********************/
//
// The container is just a header that tracks all of the crap we
// read from a BG3D file.
//

static void InitBG3DContainer(void)
{
MOGroupObject	*rootGroup;

	gBG3D_CurrentContainer = AllocPtr(sizeof(BG3DFileContainer));
	if (gBG3D_CurrentContainer == nil)
		DoFatalAlert("InitBG3DContainer: AllocPtr failed!");

	gBG3D_CurrentContainer->numMaterials =	0;			// no materials yet


			/* CREATE NEW GROUP OBJECT */

	rootGroup = MO_CreateNewObjectOfType(MO_TYPE_GROUP, 0, nil);
	if (rootGroup == nil)
		DoFatalAlert("InitBG3DContainer: MO_CreateNewObjectOfType failed");

	gBG3D_CurrentContainer->root 	= rootGroup;		// root is an empty group
	gBG3D_CurrentGroup 				= rootGroup;

}



/***************** PRELOAD TEXTURE MATERIALS ***********************/
//
// Called when we're done importing the file.  It scans all of the materials
// that were loaded and uploads all of the textures to OpenGL.
//

static void PreLoadTextureMaterials(void)
{
int					i, num,w,h;
MOMaterialObject	*mat;
MOMaterialData		*matData;
void				*pixels;

	num = gBG3D_CurrentContainer->numMaterials;


	for (i = 0; i < num; i++)
	{
		mat = gBG3D_CurrentContainer->materials[i];
		matData = &mat->objectData;

		if (matData->numMipmaps > 0)							// see if has textures
		{

				/* GET TEXTURE INFO */

			pixels 		= matData->texturePixels[0];			// get ptr to pixel buffer
			w 			= matData->width;						// get width
			h 			= matData->height;						// get height


				/* LOAD INTO OPENGL */

			matData->textureName[0] = OGL_TextureMap_Load(
				pixels,
				w,
				h,
				matData->pixelSrcFormat,
				matData->pixelDstFormat,
				GL_UNSIGNED_BYTE);


			/* DISPOSE ORIGINAL PIXELS */

			SafeDisposePtr(pixels);
			matData->texturePixels[0] = nil;
		}

				/* KEEP MUSIC PLAYING */

//		if (gSongPlayingFlag)
//			MoviesTask(gSongMovie, 0);

	}
}


#pragma mark -

/******************* DISPOSE ALL BG3D CONTAINERS ****************/

void DisposeAllBG3DContainers(void)
{
int	i;

	for (i = 0; i < MAX_BG3D_GROUPS; i++)
	{
		if (gBG3DContainerList[i])
			DisposeBG3DContainer(i);
	}
}


/************************** DISPOSE BG3D *****************************/

void DisposeBG3DContainer(int groupNum)
{
BG3DFileContainer	*file = gBG3DContainerList[groupNum];			// point to this file's container object
int					i;

	if (file == nil)
		DoFatalAlert("DisposeBG3DContainer: this BG3D container is already gone.");

			/* DISPOSE OF ALL MATERIALS */

	for (i = 0; i < file->numMaterials; i++)
	{
		MO_DisposeObjectReference(file->materials[i]);
	}


		/* DISPOSE OF ROOT OBJECT/GROUP */

	MO_DisposeObjectReference(file->root);


		/* FREE THE CONTAINER'S MEMORY */

	SafeDisposePtr((Ptr)gBG3DContainerList[groupNum]);
	gBG3DContainerList[groupNum] = nil;								// its gone
}

#pragma mark -


/*************** BG3D:  SET CONTAINER MATERIAL FLAGS *********************/
//
// Sets the material flags for this object's vertex array
//

void BG3D_SetContainerMaterialFlags(short group, short type, uint32_t flags)
{
MOVertexArrayObject	*mo;
MOVertexArrayData	*va;
MOMaterialObject	*mat;
int					n,i;

	mo = gBG3DGroupList[group][type];			// point to this model

	if ((mo->objectHeader.type != MO_TYPE_GEOMETRY) || (mo->objectHeader.subType != MO_GEOMETRY_SUBTYPE_VERTEXARRAY))
		DoFatalAlert("BG3D_SetObjectTextureWrappingFlags:  object isnt a vertex array");

	va = &mo->objectData;								// point to vertex array data

	n = va->numMaterials;
	if (n <= 0)											// make sure there are materials
		DoFatalAlert("BG3D_SetObjectTextureWrappingFlags:  no materials!");

	for (i = 0; i < n; i++)
	{
		mat = va->materials[i];							// get pointer to material
		mat->objectData.flags |= flags;					// set flags
	}
}












