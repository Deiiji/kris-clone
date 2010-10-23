/**
 * @file llfloatermodelpreview.h
 * @brief LLFloaterModelPreview class definition
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLFLOATERMODELPREVIEW_H
#define LL_LLFLOATERMODELPREVIEW_H

#include "llfloaternamedesc.h"

#include "lldynamictexture.h"
#include "llquaternion.h"
#include "llmeshrepository.h"
#include "llmodel.h"
#include "llthread.h"

class LLComboBox;
class LLJoint;
class LLViewerJointMesh;
class LLVOAvatar;
class LLTextBox;
class LLVertexBuffer;
class LLModelPreview;
class LLFloaterModelPreview;
class daeElement;
class domProfile_COMMON;
class domInstance_geometry;

class LLPhysicsDecompFloater : public LLFloater
{
public:

	LLPhysicsDecompFloater(LLSD& key);
	~LLPhysicsDecompFloater();
};

class LLModelLoader : public LLThread
{
public:
	typedef enum
	{
		STARTING = 0,
		READING_FILE,
		CREATING_FACES,
		GENERATING_VERTEX_BUFFERS,
		GENERATING_LOD,
		DONE,
	} eLoadState;

	U32 mState;
	std::string mFilename;
	S32 mLod;
	LLModelPreview* mPreview;
	LLMatrix4 mTransform;
	BOOL mFirstTransform;
	LLVector3 mExtents[2];

	std::map<daeElement*, LLPointer<LLModel> > mModel;
	
	typedef std::vector<LLPointer<LLModel> > model_list;
	model_list mModelList;

	typedef std::vector<LLModelInstance> model_instance_list;
	
	typedef std::map<LLMatrix4, model_instance_list > scene;

	scene mScene;

	typedef std::queue<LLPointer<LLModel> > model_queue;

	//queue of models that need a physics rep
	model_queue mPhysicsQ;

	LLModelLoader(std::string filename, S32 lod, LLModelPreview* preview);

	virtual void run();
	
	void processElement(daeElement* element);
	std::vector<LLImportMaterial> getMaterials(LLModel* model, domInstance_geometry* instance_geo);
	LLImportMaterial profileToMaterial(domProfile_COMMON* material);
	std::string getElementLabel(daeElement *element);
	LLColor4 getDaeColor(daeElement* element);

	//map of avatar joints as named in COLLADA assets to internal joint names
	std::map<std::string, std::string> mJointMap;
};

class LLFloaterModelPreview : public LLFloater
{
public:
	
	class DecompRequest : public LLPhysicsDecomp::Request
	{
	public:
		S32 mContinue;
		LLPointer<LLModel> mModel;
		
		DecompRequest(const std::string& stage, LLModel* mdl);
		virtual S32 statusCallback(const char* status, S32 p1, S32 p2);
		virtual void completed();
		
	};
	static LLFloaterModelPreview* sInstance;
	
	LLFloaterModelPreview(const LLSD& key);
	virtual ~LLFloaterModelPreview();
	
	virtual BOOL postBuild();
	
	BOOL handleMouseDown(S32 x, S32 y, MASK mask);
	BOOL handleMouseUp(S32 x, S32 y, MASK mask);
	BOOL handleHover(S32 x, S32 y, MASK mask);
	BOOL handleScrollWheel(S32 x, S32 y, S32 clicks); 
	
	static void onMouseCaptureLostModelPreview(LLMouseHandler*);
	static void setUploadAmount(S32 amount) { sUploadAmount = amount; }
	
	static void onBrowseHighLOD(void* data);
	static void onBrowseMediumLOD(void* data); 
	static void onBrowseLowLOD(void* data);
	static void onBrowseLowestLOD(void* data);
	
	static void onUpload(void* data);
	
	static void onConsolidate(void* data);
	static void onScrubMaterials(void* data);
	static void onDecompose(void* data);
	static void onModelDecompositionComplete(LLModel* model, std::vector<LLPointer<LLVertexBuffer> >& physics_mesh);
	
	static void refresh(LLUICtrl* ctrl, void* data);
	
	void updateResourceCost();
	
	void			loadModel(S32 lod);
	
protected:
	friend class LLModelPreview;
	friend class LLMeshFilePicker;
	friend class LLPhysicsDecomp;
	friend class LLPhysicsDecompFloater;
	
	static void		onDebugScaleCommit(LLUICtrl*, void*);
	static void		onUploadJointsCommit(LLUICtrl*,void*);
	static void		onUploadSkinCommit(LLUICtrl*,void*);
	
	static void		onPreviewLODCommit(LLUICtrl*,void*);
	
	static void		onHighLODCommit(LLUICtrl*,void*);
	static void		onMediumLODCommit(LLUICtrl*,void*);
	static void		onLowLODCommit(LLUICtrl*,void*);
	static void		onLowestLODCommit(LLUICtrl*,void*);
	static void		onPhysicsLODCommit(LLUICtrl*,void*);
	
	static void		onHighLimitCommit(LLUICtrl*,void*);
	static void		onMediumLimitCommit(LLUICtrl*,void*);
	static void		onLowLimitCommit(LLUICtrl*,void*);
	static void		onLowestLimitCommit(LLUICtrl*,void*);
	static void		onPhysicsLimitCommit(LLUICtrl*,void*);
	
	static void		onSmoothNormalsCommit(LLUICtrl*,void*);
	
	static void		onAutoFillCommit(LLUICtrl*,void*);
	static void		onShowEdgesCommit(LLUICtrl*,void*);
	
	static void		onExplodeCommit(LLUICtrl*, void*);
	
	static void onPhysicsParamCommit(LLUICtrl* ctrl, void* userdata);
	static void onPhysicsStageExecute(LLUICtrl* ctrl, void* userdata);
	static void onPhysicsStageCancel(LLUICtrl* ctrl, void* userdata);
	static void onClosePhysicsFloater(LLUICtrl* ctrl, void* userdata);
	
	void			draw();
	static void		setLODMode(S32 lod, void* userdata);
	void			setLODMode(S32 lod, S32 which_mode);
	
	static void		setLimit(S32 lod, void* userdata);
	void			setLimit(S32 lod, S32 limit);
	
	void showDecompFloater();
	
	LLModelPreview*	mModelPreview;
	
	LLFloater* mDecompFloater;
	LLPhysicsDecomp::decomp_params mDecompParams;
	
	S32				mLastMouseX;
	S32				mLastMouseY;
	LLRect			mPreviewRect;
	U32				mGLName;
	BOOL			mLoading;
	static S32		sUploadAmount;
	
	LLPointer<DecompRequest> mCurRequest;
	
	
};

class LLModelPreview : public LLViewerDynamicTexture, public LLMutex
{
 public:
	
	 LLModelPreview(S32 width, S32 height, LLFloaterModelPreview* fmp);
	virtual ~LLModelPreview();

	void resetPreviewTarget();
	void setPreviewTarget(F32 distance);
	void setTexture(U32 name) { mTextureName = name; }

	BOOL render();
	void update();
	void genBuffers(S32 lod, bool skinned);
	void clearBuffers();
	void refresh();
	void rotate(F32 yaw_radians, F32 pitch_radians);
	void zoom(F32 zoom_amt);
	void pan(F32 right, F32 up);
	virtual BOOL needsRender() { return mNeedsUpdate; }
	void setPreviewLOD(S32 lod);
	void clearModel(S32 lod);
	void loadModel(std::string filename, S32 lod);
	void loadModelCallback(S32 lod);
	void genLODs(S32 which_lod = -1);
	void smoothNormals();
	void consolidate();
	void scrubMaterials();
	U32 calcResourceCost();
	void rebuildUploadData();
	void clearIncompatible(S32 lod);
	void updateStatusMessages();
	bool containsRiggedAsset( void );

	static void	textureLoadedCallback( BOOL success, LLViewerFetchedTexture *src_vi, LLImageRaw* src, LLImageRaw* src_aux, S32 discard_level, BOOL final, void* userdata );

 protected:
	friend class LLFloaterModelPreview;
	friend class LLFloaterModelPreview::DecompRequest;
	friend class LLPhysicsDecomp;

	LLFloaterModelPreview* mFMP;

	BOOL        mNeedsUpdate;
	bool		mDirty;
	U32         mTextureName;
	F32			mCameraDistance;
	F32			mCameraYaw;
	F32			mCameraPitch;
	F32			mCameraZoom;
	LLVector3	mCameraOffset;
	LLVector3	mPreviewTarget;
	LLVector3	mPreviewScale;
	S32			mPreviewLOD;
	U32			mResourceCost;
	S32			mLODMode[LLModel::NUM_LODS];
	S32			mLimit[LLModel::NUM_LODS];
	
	LLModelLoader* mModelLoader;


	LLModelLoader::scene mScene[LLModel::NUM_LODS];
	LLModelLoader::scene mBaseScene;

	LLModelLoader::model_list mModel[LLModel::NUM_LODS];
	LLModelLoader::model_list mBaseModel;

	std::map<LLPointer<LLModel>, U32> mGroup;
	std::map<LLPointer<LLModel>, U32> mObject;
	std::map<LLPointer<LLModel>, std::vector<U32> > mPatch;
	std::map<LLPointer<LLModel>, F32> mPercentage;

	std::map<LLPointer<LLModel>, std::vector<LLPointer<LLVertexBuffer> > > mPhysicsMesh;

	LLMeshUploadThread::instance_list mUploadData;
	std::set<LLPointer<LLViewerFetchedTexture> > mTextureSet;

	//map of vertex buffers to models (one vertex buffer in vector per face in model
	std::map<LLModel*, std::vector<LLPointer<LLVertexBuffer> > > mVertexBuffer[LLModel::NUM_LODS+1];
};


#endif  // LL_LLFLOATERMODELPREVIEW_H
