/** 
 * @file llfloatermodelpreview.cpp
 * @brief LLFloaterModelPreview class implementation
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

#include "llviewerprecompiledheaders.h"

#include "dae.h"
//#include "dom.h"
#include "dom/domAsset.h"
#include "dom/domBind_material.h"
#include "dom/domCOLLADA.h"
#include "dom/domConstants.h"
#include "dom/domController.h"
#include "dom/domEffect.h"
#include "dom/domGeometry.h"
#include "dom/domInstance_geometry.h"
#include "dom/domInstance_material.h"
#include "dom/domInstance_node.h"
#include "dom/domInstance_effect.h"
#include "dom/domMaterial.h"
#include "dom/domMatrix.h"
#include "dom/domNode.h"
#include "dom/domProfile_COMMON.h"
#include "dom/domRotate.h"
#include "dom/domScale.h"
#include "dom/domTranslate.h"
#include "dom/domVisual_scene.h"

#include "llfloatermodelpreview.h"

#include "llfilepicker.h"
#include "llimagebmp.h"
#include "llimagetga.h"
#include "llimagejpeg.h"
#include "llimagepng.h"

#include "llagent.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "lldatapacker.h"
#include "lldrawable.h"
#include "lldrawpoolavatar.h"
#include "llrender.h"
#include "llface.h"
#include "lleconomy.h"
#include "llfocusmgr.h"
#include "llfloaterperms.h"
#include "llmatrix4a.h"
#include "llmeshrepository.h"
#include "llsdutil_math.h"
#include "lltextbox.h"
#include "lltoolmgr.h"
#include "llui.h"
#include "llvector4a.h"
#include "llviewercamera.h"
#include "llviewerwindow.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "pipeline.h"
#include "lluictrlfactory.h"
#include "llviewermenufile.h"
#include "llviewerregion.h"
#include "llstring.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llsliderctrl.h"
#include "llspinctrl.h"
#include "llvfile.h"
#include "llvfs.h"


#include "glod/glod.h"

//static
S32 LLFloaterModelPreview::sUploadAmount = 10;
LLFloaterModelPreview* LLFloaterModelPreview::sInstance = NULL;

const S32 PREVIEW_BORDER_WIDTH = 2;
const S32 PREVIEW_RESIZE_HANDLE_SIZE = S32(RESIZE_HANDLE_WIDTH * OO_SQRT2) + PREVIEW_BORDER_WIDTH;
const S32 PREVIEW_HPAD = PREVIEW_RESIZE_HANDLE_SIZE;
const S32 PREF_BUTTON_HEIGHT = 16 + 7 + 16;
const S32 PREVIEW_TEXTURE_HEIGHT = 300;

void drawBoxOutline(const LLVector3& pos, const LLVector3& size);

std::string limit_name[] =
{
	"lowest limit",
	"low limit",
	"medium limit",
	"high limit",
	"physics limit",

	"I went off the end of the limit_name array.  Me so smart."
};

std::string info_name[] =
{
	"lowest info",
	"low info",
	"medium info",
	"high info",
	"physics info",

	"I went off the end of the info_name array.  Me so smart."
};

bool validate_face(const LLVolumeFace& face)
{
	for (U32 i = 0; i < face.mNumIndices; ++i)
	{
		if (face.mIndices[i] >= face.mNumVertices)
		{
			llwarns << "Face has invalid index." << llendl;
			return false;
		}
	}

	return true;
}

bool validate_model(const LLModel* mdl)
{
	if (mdl->getNumVolumeFaces() == 0)
	{
		llwarns << "Model has no faces!" << llendl;
		return false;
	}
	
	for (S32 i = 0; i < mdl->getNumVolumeFaces(); ++i)
	{
		if (mdl->getVolumeFace(i).mNumVertices == 0)
		{
			llwarns << "Face has no vertices." << llendl;
			return false;
		}

		if (mdl->getVolumeFace(i).mNumIndices == 0)
		{
			llwarns << "Face has no indices." << llendl;
			return false;
		}

		if (!validate_face(mdl->getVolumeFace(i)))
		{
			return false;
		}
	}

	return true;
}

BOOL stop_gloderror()
{
	GLuint error = glodGetError();

	if (error != GLOD_NO_ERROR)
	{
		llwarns << "GLOD error detected, cannot generate LOD: " << std::hex << error << llendl;
		return TRUE;
	}

	return FALSE;
}

LLPhysicsDecompFloater::LLPhysicsDecompFloater(LLSD& key)
: LLFloater(key)
{

}

LLPhysicsDecompFloater::~LLPhysicsDecompFloater()
{
	if (LLFloaterModelPreview::sInstance && LLFloaterModelPreview::sInstance->mDecompFloater)
	{
		LLFloaterModelPreview::sInstance->mDecompFloater = NULL;
	}
}

class LLMeshFilePicker : public LLFilePickerThread
{
public:
	LLFloaterModelPreview* mFMP;
	S32 mLOD;

	LLMeshFilePicker(LLFloaterModelPreview* fmp, S32 lod)
		: LLFilePickerThread(LLFilePicker::FFLOAD_COLLADA)
	{
		mFMP = fmp;
		mLOD = lod;
	}

	virtual void notify(const std::string& filename)
	{
		mFMP->mModelPreview->loadModel(mFile, mLOD);
	}
};


//-----------------------------------------------------------------------------
// LLFloaterModelPreview()
//-----------------------------------------------------------------------------
LLFloaterModelPreview::LLFloaterModelPreview(const LLSD& key) : 
	LLFloater(key)
{
	sInstance = this;
	mLastMouseX = 0;
	mLastMouseY = 0;
	mGLName = 0;
	mLoading = FALSE;
	mDecompFloater = NULL;
}

//-----------------------------------------------------------------------------
// postBuild()
//-----------------------------------------------------------------------------
BOOL LLFloaterModelPreview::postBuild()
{
	if (!LLFloater::postBuild())
	{
		return FALSE;
	}

	childSetCommitCallback("high detail combo", onHighLODCommit, this);
	childSetCommitCallback("medium detail combo", onMediumLODCommit, this);
	childSetCommitCallback("low detail combo", onLowLODCommit, this);
	childSetCommitCallback("lowest detail combo", onLowestLODCommit, this);
	childSetCommitCallback("physics detail combo", onPhysicsLODCommit, this);


	childSetCommitCallback("high limit", onHighLimitCommit, this);
	childSetCommitCallback("medium limit", onMediumLimitCommit, this);
	childSetCommitCallback("low limit", onLowLimitCommit, this);
	childSetCommitCallback("lowest limit", onLowestLimitCommit, this);
	childSetCommitCallback("physics limit", onPhysicsLimitCommit, this);

	childSetCommitCallback("smooth normals", onSmoothNormalsCommit, this);

	childSetCommitCallback("show edges", onShowEdgesCommit, this);
	childSetCommitCallback("auto fill", onAutoFillCommit, this);

	childSetTextArg("status", "[STATUS]", getString("status_idle"));

	for (S32 lod = 0; lod < LLModel::NUM_LODS; ++lod)
	{
		if (lod == LLModel::LOD_PHYSICS)
		{
			childSetTextArg(info_name[lod], "[TRIANGLES]", std::string("0"));
			childSetTextArg(info_name[lod], "[HULLS]", std::string("0"));
			childSetTextArg(info_name[lod], "[POINTS]", std::string("0"));
		}
		else
		{
			childSetTextArg(info_name[lod], "[TRIANGLES]", std::string("0"));
			childSetTextArg(info_name[lod], "[VERTICES]", std::string("0"));
			childSetTextArg(info_name[lod], "[SUBMESHES]", std::string("0"));
			std::string msg = getString("required");
			childSetTextArg(info_name[lod], "[MESSAGE]", msg);
		}

		childSetVisible(limit_name[lod], FALSE);
	}

	//childSetLabelArg("ok_btn", "[AMOUNT]", llformat("%d",sUploadAmount));
	childSetAction("ok_btn", onUpload, this);

	childSetAction("consolidate", onConsolidate, this);
	childSetAction("scrub materials", onScrubMaterials, this);

	childSetAction("decompose_btn", onDecompose, this);

	childSetCommitCallback("preview_lod_combo", onPreviewLODCommit, this);
	
	childSetCommitCallback("upload_skin", onUploadSkinCommit, this);
	childSetCommitCallback("upload_joints", onUploadJointsCommit, this);

	childSetCommitCallback("debug scale", onDebugScaleCommit, this);

	childDisable("upload_skin");
	childDisable("upload_joints");

	const U32 width = 512;
	const U32 height = 512;

	mPreviewRect.set(getRect().getWidth()-PREVIEW_HPAD-width,
				PREVIEW_HPAD+height,
				getRect().getWidth()-PREVIEW_HPAD,
				PREVIEW_HPAD);

	mModelPreview = new LLModelPreview(512, 512, this);
	mModelPreview->setPreviewTarget(16.f);
	
	return TRUE;
}

//-----------------------------------------------------------------------------
// LLFloaterModelPreview()
//-----------------------------------------------------------------------------
LLFloaterModelPreview::~LLFloaterModelPreview()
{
	sInstance = NULL;

	if ( mModelPreview->containsRiggedAsset() )
	{
		gAgentAvatarp->resetJointPositions();
	}
	
	delete mModelPreview;
	
	if (mGLName)
	{
		LLImageGL::deleteTextures(1, &mGLName );
	}

	if (mDecompFloater)
	{
		mDecompFloater->closeFloater();
		mDecompFloater = NULL;
	}	
}

void LLFloaterModelPreview::loadModel(S32 lod)
{
	mLoading = TRUE;

	(new LLMeshFilePicker(this, lod))->getFile();
}

void LLFloaterModelPreview::setLODMode(S32 lod, S32 mode)
{
	if (mode == 0)
	{
		if (lod != LLModel::LOD_PHYSICS)
		{
			for (S32 i = lod; i >= 0; i--)
			{
				mModelPreview->clearModel(i);
			}
		}
		else
		{
			mModelPreview->clearModel(lod);
		}

		mModelPreview->refresh();
		mModelPreview->calcResourceCost();
	}
	else if (mode == 1)
	{
		loadModel(lod);
	}
	else if (mode != mModelPreview->mLODMode[lod])
	{
		mModelPreview->mLODMode[lod] = mode;
		mModelPreview->genLODs(lod);
	}

	mModelPreview->setPreviewLOD(lod);
	
	
	LLSpinCtrl* lim = getChild<LLSpinCtrl>(limit_name[lod], TRUE);

	if (mode == 2) //triangle count
	{
		U32 tri_count = 0;
		for (LLModelLoader::model_list::iterator iter = mModelPreview->mBaseModel.begin();
				iter != mModelPreview->mBaseModel.end(); ++iter)
		{
			tri_count += (*iter)->getNumTriangles();
		}

		lim->setMaxValue(tri_count);
		lim->setVisible(TRUE);
	}
	else
	{
		lim->setVisible(FALSE);
	}
}

void LLFloaterModelPreview::setLimit(S32 lod, S32 limit)
{
	if (limit != mModelPreview->mLimit[lod])
	{
		mModelPreview->mLimit[lod] = limit;
		mModelPreview->genLODs(lod);
		mModelPreview->setPreviewLOD(lod);
	}
}

//static 
void LLFloaterModelPreview::onDebugScaleCommit(LLUICtrl*,void* userdata)
{
	LLFloaterModelPreview *fp =(LLFloaterModelPreview *)userdata;
	
	if (!fp->mModelPreview)
	{
		return;
	}

	fp->mModelPreview->calcResourceCost();
}

//static 
void LLFloaterModelPreview::onUploadJointsCommit(LLUICtrl*,void* userdata)
{
	LLFloaterModelPreview *fp =(LLFloaterModelPreview *)userdata;
	
	if (!fp->mModelPreview)
	{
		return;
	}

	fp->mModelPreview->refresh();
}

//static 
void LLFloaterModelPreview::onUploadSkinCommit(LLUICtrl*,void* userdata)
{
	LLFloaterModelPreview *fp =(LLFloaterModelPreview *)userdata;
	
	if (!fp->mModelPreview)
	{
		return;
	}

	fp->mModelPreview->refresh();
	fp->mModelPreview->resetPreviewTarget();
	fp->mModelPreview->clearBuffers();
}
	
//static
void LLFloaterModelPreview::onPreviewLODCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview *fp =(LLFloaterModelPreview *)userdata;
	
	if (!fp->mModelPreview)
	{
		return;
	}

	S32 which_mode = 0;

	LLCtrlSelectionInterface* iface = fp->childGetSelectionInterface("preview_lod_combo");
	if (iface)
	{
		which_mode = iface->getFirstSelectedIndex();
	}
	fp->mModelPreview->setPreviewLOD(which_mode);
}

//static 
void LLFloaterModelPreview::setLODMode(S32 lod, void* userdata)
{
	LLFloaterModelPreview *fp =(LLFloaterModelPreview *)userdata;
	
	if (!fp->mModelPreview)
	{
		return;
	}

	S32 which_mode = 0;

	std::string combo_name[] =
	{
		"lowest detail combo",
		"low detail combo",
		"medium detail combo",
		"high detail combo",
		"physics detail combo",

		"I went off the end of the combo_name array.  Me so smart."
	};

	LLCtrlSelectionInterface* iface = fp->childGetSelectionInterface(combo_name[lod]);
	if (iface)
	{
		which_mode = iface->getFirstSelectedIndex();
	}

	fp->setLODMode(lod, which_mode);
}

//static 
void LLFloaterModelPreview::onHighLODCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLODMode(3, userdata);
}

//static 
void LLFloaterModelPreview::onMediumLODCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLODMode(2, userdata);
}

//static 
void LLFloaterModelPreview::onLowLODCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLODMode(1, userdata);
}

//static 
void LLFloaterModelPreview::onLowestLODCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLODMode(0, userdata);
}

//static 
void LLFloaterModelPreview::onPhysicsLODCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLODMode(4, userdata);
}

//static 
void LLFloaterModelPreview::setLimit(S32 lod, void* userdata)
{
	LLFloaterModelPreview *fp =(LLFloaterModelPreview *)userdata;
	
	if (!fp->mModelPreview)
	{
		return;
	}

	S32 limit = fp->childGetValue(limit_name[lod]).asInteger();

	
	fp->setLimit(lod, limit);
}

//static 
void LLFloaterModelPreview::onHighLimitCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLimit(3, userdata);
}

//static 
void LLFloaterModelPreview::onMediumLimitCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLimit(2, userdata);
}

//static 
void LLFloaterModelPreview::onLowLimitCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLimit(1, userdata);
}

//static 
void LLFloaterModelPreview::onLowestLimitCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLimit(0, userdata);
}

//static 
void LLFloaterModelPreview::onPhysicsLimitCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview::setLimit(4, userdata);
}

//static
void LLFloaterModelPreview::onSmoothNormalsCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview* fp = (LLFloaterModelPreview*) userdata;

	fp->mModelPreview->smoothNormals();
}

//static
void LLFloaterModelPreview::onShowEdgesCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview* fp = (LLFloaterModelPreview*) userdata;

	fp->mModelPreview->refresh();
}

//static
void LLFloaterModelPreview::onExplodeCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview* fp = LLFloaterModelPreview::sInstance;

	fp->mModelPreview->refresh();
}

//static 
void LLFloaterModelPreview::onAutoFillCommit(LLUICtrl* ctrl, void* userdata)
{
	LLFloaterModelPreview* fp = (LLFloaterModelPreview*) userdata;

	fp->mModelPreview->genLODs();
}


//-----------------------------------------------------------------------------
// draw()
//-----------------------------------------------------------------------------
void LLFloaterModelPreview::draw()
{
	LLFloater::draw();
	LLRect r = getRect();

	mModelPreview->update();
	
	if (!mLoading)
	{
		childSetTextArg("status", "[STATUS]", getString("status_idle"));
	}
	
	childSetTextArg("description_label", "[PRIM_COST]", llformat("%d", mModelPreview->mResourceCost));
	childSetTextArg("description_label", "[TEXTURES]", llformat("%d", mModelPreview->mTextureSet.size()));

	if (mDecompFloater)
	{
		if (mCurRequest.notNull())
		{
			mDecompFloater->childSetText("status", mCurRequest->mStatusMessage);
		}
		else
		{
			const std::string idle("Idle.");
			mDecompFloater->childSetText("status", idle);
		}
	}

	U32 resource_cost = mModelPreview->mResourceCost*10;

	if (childGetValue("upload_textures").asBoolean())
	{
		resource_cost += mModelPreview->mTextureSet.size()*10;
	}
	
	childSetLabelArg("ok_btn", "[AMOUNT]", llformat("%d", resource_cost));
	
	if (mModelPreview)
	{
		gGL.color3f(1.f, 1.f, 1.f);

		gGL.getTexUnit(0)->bind(mModelPreview);
		
		gGL.begin( LLRender::QUADS );
		{
			gGL.texCoord2f(0.f, 1.f);
			gGL.vertex2i(mPreviewRect.mLeft, mPreviewRect.mTop);
			gGL.texCoord2f(0.f, 0.f);
			gGL.vertex2i(mPreviewRect.mLeft, mPreviewRect.mBottom);
			gGL.texCoord2f(1.f, 0.f);
			gGL.vertex2i(mPreviewRect.mRight, mPreviewRect.mBottom);
			gGL.texCoord2f(1.f, 1.f);
			gGL.vertex2i(mPreviewRect.mRight, mPreviewRect.mTop);
		}
		gGL.end();

		gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	}
}

//-----------------------------------------------------------------------------
// handleMouseDown()
//-----------------------------------------------------------------------------
BOOL LLFloaterModelPreview::handleMouseDown(S32 x, S32 y, MASK mask)
{
	if (mPreviewRect.pointInRect(x, y))
	{
		bringToFront( x, y );
		gFocusMgr.setMouseCapture(this);
		gViewerWindow->hideCursor();
		mLastMouseX = x;
		mLastMouseY = y;
		return TRUE;
	}

	return LLFloater::handleMouseDown(x, y, mask);
}

//-----------------------------------------------------------------------------
// handleMouseUp()
//-----------------------------------------------------------------------------
BOOL LLFloaterModelPreview::handleMouseUp(S32 x, S32 y, MASK mask)
{
	gFocusMgr.setMouseCapture(FALSE);
	gViewerWindow->showCursor();
	return LLFloater::handleMouseUp(x, y, mask);
}

//-----------------------------------------------------------------------------
// handleHover()
//-----------------------------------------------------------------------------
BOOL LLFloaterModelPreview::handleHover	(S32 x, S32 y, MASK mask)
{
	MASK local_mask = mask & ~MASK_ALT;

	if (mModelPreview && hasMouseCapture())
	{
		if (local_mask == MASK_PAN)
		{
			// pan here
			mModelPreview->pan((F32)(x - mLastMouseX) * -0.005f, (F32)(y - mLastMouseY) * -0.005f);
		}
		else if (local_mask == MASK_ORBIT)
		{
			F32 yaw_radians = (F32)(x - mLastMouseX) * -0.01f;
			F32 pitch_radians = (F32)(y - mLastMouseY) * 0.02f;
			
			mModelPreview->rotate(yaw_radians, pitch_radians);
		}
		else 
		{
		
			F32 yaw_radians = (F32)(x - mLastMouseX) * -0.01f;
			F32 zoom_amt = (F32)(y - mLastMouseY) * 0.02f;
			
			mModelPreview->rotate(yaw_radians, 0.f);
			mModelPreview->zoom(zoom_amt);
		}

		
		mModelPreview->refresh();
		
		LLUI::setMousePositionLocal(this, mLastMouseX, mLastMouseY);
	}

	if (!mPreviewRect.pointInRect(x, y) || !mModelPreview)
	{
		return LLFloater::handleHover(x, y, mask);
	}
	else if (local_mask == MASK_ORBIT)
	{
		gViewerWindow->setCursor(UI_CURSOR_TOOLCAMERA);
	}
	else if (local_mask == MASK_PAN)
	{
		gViewerWindow->setCursor(UI_CURSOR_TOOLPAN);
	}
	else
	{
		gViewerWindow->setCursor(UI_CURSOR_TOOLZOOMIN);
	}

	return TRUE;
}

//-----------------------------------------------------------------------------
// handleScrollWheel()
//-----------------------------------------------------------------------------
BOOL LLFloaterModelPreview::handleScrollWheel(S32 x, S32 y, S32 clicks)
{
	if (mPreviewRect.pointInRect(x, y) && mModelPreview)
	{
		mModelPreview->zoom((F32)clicks * -0.2f);
		mModelPreview->refresh();
	}

	return TRUE;
}

//static
void LLFloaterModelPreview::onPhysicsParamCommit(LLUICtrl* ctrl, void* data)
{
	if (LLConvexDecomposition::getInstance() == NULL)
	{
		llinfos << "convex decomposition tool is a stub on this platform. cannot get decomp." << llendl;
		return;
	}

	if (sInstance)
	{
		LLCDParam* param = (LLCDParam*) data;
		sInstance->mDecompParams[param->mName] = ctrl->getValue();
	}
}

//static
void LLFloaterModelPreview::onPhysicsStageExecute(LLUICtrl* ctrl, void* data)
{
	LLCDStageData* stage = (LLCDStageData*) data;
	
	LLModel* mdl = NULL;

	if (sInstance)
	{
		if (sInstance->mCurRequest.notNull())
		{
			llinfos << "Decomposition request still pending." << llendl;
			return;
		}

		if (sInstance->mModelPreview)
		{
			if (sInstance->mDecompFloater)
			{
				S32 idx = sInstance->mDecompFloater->childGetValue("model").asInteger();
				if (idx >= 0 && idx < sInstance->mModelPreview->mModel[LLModel::LOD_PHYSICS].size())
				{
					mdl = sInstance->mModelPreview->mModel[LLModel::LOD_PHYSICS][idx];
				}
			}
		}
	}
	
	if (mdl)
	{
		sInstance->mCurRequest = new DecompRequest(stage->mName, mdl);
		gMeshRepo.mDecompThread->submitRequest(sInstance->mCurRequest);
	}
}

//static
void LLFloaterModelPreview::onPhysicsStageCancel(LLUICtrl* ctrl, void*data)
{
	if (sInstance && sInstance->mCurRequest.notNull())
	{
		sInstance->mCurRequest->mContinue = 0;
	}
}

void LLFloaterModelPreview::showDecompFloater()
{
	if (!mDecompFloater)
	{
		LLSD key;
		mDecompFloater = new LLPhysicsDecompFloater(key);
	
		S32 left = 20;
		S32 right = 320;

		S32 cur_y = 30;

		{
			//add status text
			LLTextBox::Params p;
			p.name("status");
			p.rect(LLRect(left, cur_y, right-80, cur_y-20));
			mDecompFloater->addChild(LLUICtrlFactory::create<LLTextBox>(p));
		}


		{ //add cancel button
			LLButton::Params p;
			p.name("Cancel");
			p.label("Cancel");
			p.rect(LLRect(right-80, cur_y, right, cur_y-20));		
			LLButton* button = LLUICtrlFactory::create<LLButton>(p);
			button->setCommitCallback(onPhysicsStageCancel, NULL);		
			mDecompFloater->addChild(button);
		}

		cur_y += 30;


		static const LLCDStageData* stage = NULL;
		static S32 stage_count = 0;

		if (!stage && LLConvexDecomposition::getInstance() != NULL)
		{
			stage_count = LLConvexDecomposition::getInstance()->getStages(&stage);
		}

		static const LLCDParam* param = NULL;
		static S32 param_count = 0;
		if (!param && LLConvexDecomposition::getInstance() != NULL)
		{
			param_count = LLConvexDecomposition::getInstance()->getParameters(&param);
		}

		for (S32 j = stage_count-1; j >= 0; --j)
		{
			LLButton::Params p;
			p.name(stage[j].mName);
			p.label(stage[j].mName);
			p.rect(LLRect(left, cur_y, right, cur_y-20));		
			LLButton* button = LLUICtrlFactory::create<LLButton>(p);
			button->setCommitCallback(onPhysicsStageExecute, (void*) &stage[j]);		
			mDecompFloater->addChild(button);
			gMeshRepo.mDecompThread->mStageID[stage[j].mName] = j;
			cur_y += 30;
			// protected against stub by stage_count being 0 for stub above
			LLConvexDecomposition::getInstance()->registerCallback(j, LLPhysicsDecomp::llcdCallback);

			llinfos << "Physics decomp stage " << stage[j].mName << " (" << j << ") parameters:" << llendl;
			llinfos << "------------------------------------" << llendl;

			for (S32 i = 0; i < param_count; ++i)
			{
				if (param[i].mStage != j)
				{
					continue;
				}

				std::string name(param[i].mName ? param[i].mName : "");
				std::string description(param[i].mDescription ? param[i].mDescription : "");

				std::string type = "unknown";

				llinfos << name << " - " << description << llendl;

				if (param[i].mType == LLCDParam::LLCD_FLOAT)
				{
					mDecompParams[param[i].mName] = LLSD(param[i].mDefault.mFloat);
					llinfos << "Type: float, Default: " << param[i].mDefault.mFloat << llendl;

					LLSliderCtrl::Params p;
					p.name(name);
					p.label(name);
					p.rect(LLRect(left, cur_y, right, cur_y-20));
					p.min_value(param[i].mDetails.mRange.mLow.mFloat);
					p.max_value(param[i].mDetails.mRange.mHigh.mFloat);
					p.increment(param[i].mDetails.mRange.mDelta.mFloat);
					p.tool_tip(description);
					p.decimal_digits(3);
					p.initial_value(param[i].mDefault.mFloat);
					LLSliderCtrl* slider = LLUICtrlFactory::create<LLSliderCtrl>(p);
					slider->setCommitCallback(onPhysicsParamCommit, (void*) &param[i]);
					mDecompFloater->addChild(slider);
					cur_y += 30;
				}
				else if (param[i].mType == LLCDParam::LLCD_INTEGER)
				{
					mDecompParams[param[i].mName] = LLSD(param[i].mDefault.mIntOrEnumValue);
					llinfos << "Type: integer, Default: " << param[i].mDefault.mIntOrEnumValue << llendl;
					LLSliderCtrl::Params p;
					p.name(name);
					p.label(name);
					p.rect(LLRect(left, cur_y, right, cur_y-20));
					p.min_value(param[i].mDetails.mRange.mLow.mIntOrEnumValue);
					p.max_value(param[i].mDetails.mRange.mHigh.mIntOrEnumValue);
					p.increment(param[i].mDetails.mRange.mDelta.mIntOrEnumValue);
					p.tool_tip(description);
					p.initial_value(param[i].mDefault.mIntOrEnumValue);
					LLSliderCtrl* slider = LLUICtrlFactory::create<LLSliderCtrl>(p);
					slider->setCommitCallback(onPhysicsParamCommit, (void*) &param[i]);
					mDecompFloater->addChild(slider);	
					cur_y += 30;
				}
				else if (param[i].mType == LLCDParam::LLCD_BOOLEAN)
				{
					mDecompParams[param[i].mName] = LLSD(param[i].mDefault.mBool);
					llinfos << "Type: boolean, Default: " << (param[i].mDefault.mBool ? "True" : "False") << llendl;

					LLCheckBoxCtrl::Params p;
					p.rect(LLRect(left, cur_y, right, cur_y-20));
					p.name(name);
					p.label(name);
					p.initial_value(param[i].mDefault.mBool);
					p.tool_tip(description);
					LLCheckBoxCtrl* check_box = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
					check_box->setCommitCallback(onPhysicsParamCommit, (void*) &param[i]);
					mDecompFloater->addChild(check_box);
					cur_y += 30;
				}
				else if (param[i].mType == LLCDParam::LLCD_ENUM)
				{
					S32 cur_x = left;
					mDecompParams[param[i].mName] = LLSD(param[i].mDefault.mIntOrEnumValue);
					llinfos << "Type: enum, Default: " << param[i].mDefault.mIntOrEnumValue << llendl;

					{ //add label
						LLTextBox::Params p;
						const LLFontGL* font = (LLFontGL*) p.font();

						p.rect(LLRect(left, cur_y, left+font->getWidth(name), cur_y-20));
						p.name(name);
						p.label(name);
						p.initial_value(name);
						LLTextBox* text_box = LLUICtrlFactory::create<LLTextBox>(p);
						mDecompFloater->addChild(text_box);
						cur_x += text_box->getRect().getWidth();
					}

					{ //add combo_box
						LLComboBox::Params p;
						p.rect(LLRect(cur_x, cur_y, right-right/4, cur_y-20));
						p.name(name);
						p.label(name);
						p.tool_tip(description);

						llinfos << "Accepted values: " << llendl;
						LLComboBox* combo_box = LLUICtrlFactory::create<LLComboBox>(p);
						for (S32 k = 0; k < param[i].mDetails.mEnumValues.mNumEnums; ++k)
						{
							llinfos << param[i].mDetails.mEnumValues.mEnumsArray[k].mValue 
								<< " - " << param[i].mDetails.mEnumValues.mEnumsArray[k].mName << llendl;

							combo_box->add(param[i].mDetails.mEnumValues.mEnumsArray[k].mName, 
								LLSD::Integer(param[i].mDetails.mEnumValues.mEnumsArray[k].mValue));
						}
						combo_box->setValue(param[i].mDefault.mIntOrEnumValue);
						combo_box->setCommitCallback(onPhysicsParamCommit, (void*) &param[i]);
						mDecompFloater->addChild(combo_box);
						cur_y += 30;

					}

					llinfos << "----" << llendl;
				}
				llinfos << "-----------------------------" << llendl;
			}
		}

		cur_y += 30;
		//explode slider
		{
			LLSliderCtrl::Params p;
			p.initial_value(0);
			p.min_value(0);
			p.max_value(1);
			p.decimal_digits(2);
			p.increment(0.05f);
			p.label("Explode");
			p.name("explode");
			p.rect(LLRect(left, cur_y, right, cur_y-20));
			LLSliderCtrl* slider = LLUICtrlFactory::create<LLSliderCtrl>(p);

			mDecompFloater->addChild(slider);
			cur_y += 30;
		}

		//mesh render checkbox
		{
			LLCheckBoxCtrl::Params p;
			p.label("Mesh: ");
			p.name("render_mesh");
			p.rect(LLRect(left, cur_y, right/4, cur_y-20));
			LLCheckBoxCtrl* check = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
			check->setValue(true);
			mDecompFloater->addChild(check);
		}

		//hull render checkbox
		{
			LLCheckBoxCtrl::Params p;
			p.label("Hull: ");
			p.name("render_hull");
			p.rect(LLRect(right/4, cur_y, right/2, cur_y-20));
			LLCheckBoxCtrl* check = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
			check->setValue(true);
			mDecompFloater->addChild(check);
		}

		{ //submesh combo box label
			LLTextBox::Params p;
			p.label("Model");
			p.name("model label");
			p.rect(LLRect(right/2, cur_y, right-right/3, cur_y-20));
			LLTextBox* text_box = LLUICtrlFactory::create<LLTextBox>(p);
			text_box->setValue("Model");
			mDecompFloater->addChild(text_box);
		}

		{
			//add submesh combo box
			LLComboBox::Params p;
			p.rect(LLRect(right-right/2+p.font()->getWidth("Model"), cur_y, right, cur_y-20));
			p.name("model");
			LLComboBox* combo_box = LLUICtrlFactory::create<LLComboBox>(p);
			for (S32 i = 0; i < mModelPreview->mBaseModel.size(); ++i)
			{
				LLModel* mdl = mModelPreview->mBaseModel[i];
				combo_box->add(mdl->mLabel, i);
			}
			combo_box->setValue(0);
			mDecompFloater->addChild(combo_box);
			cur_y += 30;
		}

		mDecompFloater->childSetCommitCallback("model", LLFloaterModelPreview::refresh, LLFloaterModelPreview::sInstance);
		mDecompFloater->childSetCommitCallback("render_mesh", LLFloaterModelPreview::refresh, LLFloaterModelPreview::sInstance);
		mDecompFloater->childSetCommitCallback("render_hull", LLFloaterModelPreview::refresh, LLFloaterModelPreview::sInstance);

		mDecompFloater->setRect(LLRect(10, cur_y+20, right+20, 10)); 
	}

	mDecompFloater->childSetCommitCallback("explode", LLFloaterModelPreview::onExplodeCommit, this);

	mDecompFloater->openFloater();
}

//-----------------------------------------------------------------------------
// onMouseCaptureLost()
//-----------------------------------------------------------------------------
// static
void LLFloaterModelPreview::onMouseCaptureLostModelPreview(LLMouseHandler* handler)
{
	gViewerWindow->showCursor();
}

//-----------------------------------------------------------------------------
// LLModelLoader
//-----------------------------------------------------------------------------
LLModelLoader::LLModelLoader(std::string filename, S32 lod, LLModelPreview* preview)
: LLThread("Model Loader"), mFilename(filename), mLod(lod), mPreview(preview), mState(STARTING), mFirstTransform(TRUE)
{
	mJointMap["mPelvis"] = "mPelvis";
	mJointMap["mTorso"] = "mTorso";
	mJointMap["mChest"] = "mChest";
	mJointMap["mNeck"] = "mNeck";
	mJointMap["mHead"] = "mHead";
	mJointMap["mSkull"] = "mSkull";
	mJointMap["mEyeRight"] = "mEyeRight";
	mJointMap["mEyeLeft"] = "mEyeLeft";
	mJointMap["mCollarLeft"] = "mCollarLeft";
	mJointMap["mShoulderLeft"] = "mShoulderLeft";
	mJointMap["mElbowLeft"] = "mElbowLeft";
	mJointMap["mWristLeft"] = "mWristLeft";
	mJointMap["mCollarRight"] = "mCollarRight";
	mJointMap["mShoulderRight"] = "mShoulderRight";
	mJointMap["mElbowRight"] = "mElbowRight";
	mJointMap["mWristRight"] = "mWristRight";
	mJointMap["mHipRight"] = "mHipRight";
	mJointMap["mKneeRight"] = "mKneeRight";
	mJointMap["mAnkleRight"] = "mAnkleRight";
	mJointMap["mFootRight"] = "mFootRight";
	mJointMap["mToeRight"] = "mToeRight";
	mJointMap["mHipLeft"] = "mHipLeft";
	mJointMap["mKneeLeft"] = "mKneeLeft";
	mJointMap["mAnkleLeft"] = "mAnkleLeft";
	mJointMap["mFootLeft"] = "mFootLeft";
	mJointMap["mToeLeft"] = "mToeLeft";

	mJointMap["avatar_mPelvis"] = "mPelvis";
	mJointMap["avatar_mTorso"] = "mTorso";
	mJointMap["avatar_mChest"] = "mChest";
	mJointMap["avatar_mNeck"] = "mNeck";
	mJointMap["avatar_mHead"] = "mHead";
	mJointMap["avatar_mSkull"] = "mSkull";
	mJointMap["avatar_mEyeRight"] = "mEyeRight";
	mJointMap["avatar_mEyeLeft"] = "mEyeLeft";
	mJointMap["avatar_mCollarLeft"] = "mCollarLeft";
	mJointMap["avatar_mShoulderLeft"] = "mShoulderLeft";
	mJointMap["avatar_mElbowLeft"] = "mElbowLeft";
	mJointMap["avatar_mWristLeft"] = "mWristLeft";
	mJointMap["avatar_mCollarRight"] = "mCollarRight";
	mJointMap["avatar_mShoulderRight"] = "mShoulderRight";
	mJointMap["avatar_mElbowRight"] = "mElbowRight";
	mJointMap["avatar_mWristRight"] = "mWristRight";
	mJointMap["avatar_mHipRight"] = "mHipRight";
	mJointMap["avatar_mKneeRight"] = "mKneeRight";
	mJointMap["avatar_mAnkleRight"] = "mAnkleRight";
	mJointMap["avatar_mFootRight"] = "mFootRight";
	mJointMap["avatar_mToeRight"] = "mToeRight";
	mJointMap["avatar_mHipLeft"] = "mHipLeft";
	mJointMap["avatar_mKneeLeft"] = "mKneeLeft";
	mJointMap["avatar_mAnkleLeft"] = "mAnkleLeft";
	mJointMap["avatar_mFootLeft"] = "mFootLeft";
	mJointMap["avatar_mToeLeft"] = "mToeLeft";


	mJointMap["hip"] = "mPelvis";
	mJointMap["abdomen"] = "mTorso";
	mJointMap["chest"] = "mChest";
	mJointMap["neck"] = "mNeck";
	mJointMap["head"] = "mHead";
	mJointMap["figureHair"] = "mSkull";
	mJointMap["lCollar"] = "mCollarLeft";
	mJointMap["lShldr"] = "mShoulderLeft";
	mJointMap["lForeArm"] = "mElbowLeft";
	mJointMap["lHand"] = "mWristLeft";
	mJointMap["rCollar"] = "mCollarRight";
	mJointMap["rShldr"] = "mShoulderRight";
	mJointMap["rForeArm"] = "mElbowRight";
	mJointMap["rHand"] = "mWristRight";
	mJointMap["rThigh"] = "mHipRight";
	mJointMap["rShin"] = "mKneeRight";
	mJointMap["rFoot"] = "mFootRight";
	mJointMap["lThigh"] = "mHipLeft";
	mJointMap["lShin"] = "mKneeLeft";
	mJointMap["lFoot"] = "mFootLeft";
}

void stretch_extents(LLModel* model, LLMatrix4a& mat, LLVector4a& min, LLVector4a& max, BOOL& first_transform)
{
	LLVector4a box[] = 
	{
		LLVector4a(-1, 1,-1),
		LLVector4a(-1, 1, 1),
		LLVector4a(-1,-1,-1),
		LLVector4a(-1,-1, 1),
		LLVector4a( 1, 1,-1),
		LLVector4a( 1, 1, 1),
		LLVector4a( 1,-1,-1),
		LLVector4a( 1,-1, 1),
	};

	for (S32 j = 0; j < model->getNumVolumeFaces(); ++j)
	{
		const LLVolumeFace& face = model->getVolumeFace(j);
		
		LLVector4a center;
		center.setAdd(face.mExtents[0], face.mExtents[1]);
		center.mul(0.5f);
		LLVector4a size;
		size.setSub(face.mExtents[1],face.mExtents[0]);
		size.mul(0.5f);

		for (U32 i = 0; i < 8; i++)
		{
			LLVector4a t;
			t.setMul(size, box[i]);
			t.add(center);

			LLVector4a v;

			mat.affineTransform(t, v);								
			
			if (first_transform)
			{
				first_transform = FALSE;
				min = max = v;
			}
			else
			{
				update_min_max(min, max, v);
			}
		}
	}
}

void stretch_extents(LLModel* model, LLMatrix4& mat, LLVector3& min, LLVector3& max, BOOL& first_transform)
{
	LLVector4a mina, maxa;
	LLMatrix4a mata;

	mata.loadu(mat);
	mina.load3(min.mV);
	maxa.load3(max.mV);

	stretch_extents(model, mata, mina, maxa, first_transform);
	
	min.set(mina.getF32ptr());
	max.set(maxa.getF32ptr());
}

void LLModelLoader::run()
{
	DAE dae;
	domCOLLADA* dom = dae.open(mFilename);

	if (dom)
	{
		daeDatabase* db = dae.getDatabase();

		daeInt count = db->getElementCount(NULL, COLLADA_TYPE_MESH);

		daeDocument* doc = dae.getDoc(mFilename);
		if (!doc)
		{
			llwarns << "can't find internal doc" << llendl;
			return;
		}

		daeElement* root = doc->getDomRoot();
		if (!root)
		{
			llwarns << "document has no root" << llendl;
			return;
		}

		//get unit scale
		mTransform.setIdentity();

		domAsset::domUnit* unit = daeSafeCast<domAsset::domUnit>(root->getDescendant(daeElement::matchType(domAsset::domUnit::ID())));

		if (unit)
		{
			F32 meter = unit->getMeter();
			mTransform.mMatrix[0][0] = meter;
			mTransform.mMatrix[1][1] = meter;
			mTransform.mMatrix[2][2] = meter;
		}

		//get up axis rotation
		LLMatrix4 rotation;

		domUpAxisType up = UPAXISTYPE_Y_UP;  // default is Y_UP
		domAsset::domUp_axis* up_axis =
			daeSafeCast<domAsset::domUp_axis>(root->getDescendant(daeElement::matchType(domAsset::domUp_axis::ID())));

		if (up_axis)
		{
			up = up_axis->getValue();
		}
		
		if (up == UPAXISTYPE_X_UP)
		{
			rotation.initRotation(0.0f, 90.0f * DEG_TO_RAD, 0.0f);
		}
		else if (up == UPAXISTYPE_Y_UP)
		{
			rotation.initRotation(90.0f * DEG_TO_RAD, 0.0f, 0.0f);
		}

		rotation *= mTransform;
		mTransform = rotation;


		for (daeInt idx = 0; idx < count; ++idx)
		{ //build map of domEntities to LLModel
			domMesh* mesh = NULL;
			db->getElement((daeElement**) &mesh, idx, NULL, COLLADA_TYPE_MESH);

			if (mesh)
			{
				LLPointer<LLModel> model = LLModel::loadModelFromDomMesh(mesh);

				if (model.notNull() && validate_model(model))
				{
					mModelList.push_back(model);
					mModel[mesh] = model;
				}
			}
		}

		count = db->getElementCount(NULL, COLLADA_TYPE_SKIN);
		for (daeInt idx = 0; idx < count; ++idx)
		{ //add skinned meshes as instances
			domSkin* skin = NULL;
			db->getElement((daeElement**) &skin, idx, NULL, COLLADA_TYPE_SKIN);

			if (skin)
			{	
				domGeometry* geom = daeSafeCast<domGeometry>(skin->getSource().getElement());
				
				if (geom)
				{
					domMesh* mesh = geom->getMesh();
					if (mesh)
					{
						LLModel* model = mModel[mesh];
						if (model)
						{
							LLVector3 mesh_scale_vector;
							LLVector3 mesh_translation_vector;
							model->getNormalizedScaleTranslation(mesh_scale_vector, mesh_translation_vector);

							LLMatrix4 normalized_transformation;
							normalized_transformation.setTranslation(mesh_translation_vector);
							
							LLMatrix4 mesh_scale;
							mesh_scale.initScale(mesh_scale_vector);
							mesh_scale *= normalized_transformation;
							normalized_transformation = mesh_scale;

							glh::matrix4f inv_mat((F32*) normalized_transformation.mMatrix);
							inv_mat = inv_mat.inverse();
							LLMatrix4 inverse_normalized_transformation(inv_mat.m);							

							domSkin::domBind_shape_matrix* bind_mat = skin->getBind_shape_matrix();

							if (bind_mat)
							{ //get bind shape matrix
								domFloat4x4& dom_value = bind_mat->getValue();
								
								for (int i = 0; i < 4; i++)
								{
									for(int j = 0; j < 4; j++)
									{
										model->mBindShapeMatrix.mMatrix[i][j] = dom_value[i + j*4];
									}
								}

								LLMatrix4 trans = normalized_transformation;
								trans *= model->mBindShapeMatrix;
								model->mBindShapeMatrix = trans;

							}

							/*{
								LLMatrix4 rotation;
								if (up == UPAXISTYPE_X_UP)
								{
									rotation.initRotation(0.0f, 90.0f * DEG_TO_RAD, 0.0f);
								}
								else if (up == UPAXISTYPE_Z_UP)
								{
									rotation.initRotation(90.0f * DEG_TO_RAD, 90.0f * DEG_TO_RAD, 0.0f);
								}

								rotation *= model->mBindShapeMatrix;
								model->mBindShapeMatrix = rotation;
							}*/

							//The joint transfom map that we'll populate below
							std::map<std::string,LLMatrix4> jointTransforms;
							jointTransforms.clear();
							
							//Some collada setup for accessing the skeleton
							daeElement* pElement = 0;
							dae.getDatabase()->getElement( &pElement, 0, 0, "skeleton" );
							domInstance_controller::domSkeleton* pSkeleton = daeSafeCast<domInstance_controller::domSkeleton>( pElement );
							if ( pSkeleton )
							{       
								//Get the root node of the skeleton
								daeElement* pSkeletonRootNode = pSkeleton->getValue().getElement();
								if ( pSkeletonRootNode )
								{       
									//Once we have the root node - start acccessing it's joint components       
									const int jointCnt = mJointMap.size();
									std::map<std::string, std::string> :: const_iterator jointIt = mJointMap.begin();
									bool missingID = false;
									//Loop over all the possible joints within the .dae - using the allowed joint list in the ctor.
									for ( int i=0; i<jointCnt; ++i, ++jointIt )
									{
										//Build a joint for the resolver to work with
										char str[64]={0};           
										sprintf(str,"./%s",(*jointIt).second.c_str() );                   
										//llwarns<<"Joint "<< str <<llendl;
										
										//Setup the resolver
										daeSIDResolver resolver( pSkeletonRootNode, str );
										
										//Look for the joint
										domNode* pJoint = daeSafeCast<domNode>(resolver.getElement());
										if ( pJoint )
										{               
											//Pull out the translate id and store it in the jointTranslations map
											daeSIDResolver jointResolver( pJoint, "./translate" );    						   
											domTranslate* pTranslate = daeSafeCast<domTranslate>( jointResolver.getElement() );
											
											LLMatrix4 workingTransform;
											
											//Translation
											if ( pTranslate )
											{               
												domFloat3 jointTrans = pTranslate->getValue();
												LLVector3 singleJointTranslation( jointTrans[0], jointTrans[1], jointTrans[2] );
												workingTransform.setTranslation( singleJointTranslation );											
											}
											else
											{
												missingID = true;
												llwarns<< "No translation sid!" << llendl;
											}
											//Store the joint transform w/respect to it's name. 
											jointTransforms[(*jointIt).second.c_str()] = workingTransform; 
											
										}
										else
										{
											missingID = true;
											llwarns<< "Missing joint." << llendl;
										}
									}
									
									//If anything failed in regards to extracting the skeleton, joints or translation id,
									//mention it
									if ( missingID )
									{
										llwarns<< "Partial jointmap found in asset - did you mean to just have a partial map?" << llendl;
									}
									
									//Set the joint translations on the avatar
									//The joints are reset in the dtor
									jointIt = mJointMap.begin();
									for ( int i=0; i<jointCnt; ++i, ++jointIt )
									{
										std::string lookingForJoint = (*jointIt).first.c_str();
										if ( jointTransforms.find( lookingForJoint ) != jointTransforms.end() )
										{											
											LLMatrix4 jointTransform = jointTransforms[lookingForJoint];
											LLJoint* pJoint = gAgentAvatarp->getJoint( lookingForJoint );
											if ( pJoint )
											{   
												pJoint->storeCurrentXform( jointTransform.getTranslation() );												
											}
											else
											{
												//Most likely an error in the asset.
												llwarns<<"Tried to apply joint position from .dae, but it did not exist in the avatar rig." << llendl;
											}
										}
									}  		 
								}
								else
								{           
									llwarns<<"No root node in this skeleton" << llendl;
								}
							}
							else
							{
								llwarns<<"No skeleton in this asset" << llendl;
							}
							
							domSkin::domJoints* joints = skin->getJoints();

							domInputLocal_Array& joint_input = joints->getInput_array();

							for (size_t i = 0; i < joint_input.getCount(); ++i)
							{
								domInputLocal* input = joint_input.get(i);
								xsNMTOKEN semantic = input->getSemantic();

								if (strcmp(semantic, COMMON_PROFILE_INPUT_JOINT) == 0)
								{ //found joint source, fill model->mJointMap and model->mJointList
									daeElement* elem = input->getSource().getElement();
									
									domSource* source = daeSafeCast<domSource>(elem);
									if (source)
									{ 
										

										domName_array* names_source = source->getName_array();
										
										if (names_source)
										{
											domListOfNames &names = names_source->getValue();					

											for (size_t j = 0; j < names.getCount(); ++j)
											{
												std::string name(names.get(j));
												if (mJointMap.find(name) != mJointMap.end())
												{
													name = mJointMap[name];
												}
												model->mJointList.push_back(name);
												model->mJointMap[name] = j;
											}	
										}
										else
										{
											domIDREF_array* names_source = source->getIDREF_array();
											if (names_source)
											{
												xsIDREFS& names = names_source->getValue();

												for (size_t j = 0; j < names.getCount(); ++j)
												{
													std::string name(names.get(j).getID());
													if (mJointMap.find(name) != mJointMap.end())
													{
														name = mJointMap[name];
													}
													model->mJointList.push_back(name);
													model->mJointMap[name] = j;
												}
											}
										}
									}
								}
								else if (strcmp(semantic, COMMON_PROFILE_INPUT_INV_BIND_MATRIX) == 0)
								{ //found inv_bind_matrix array, fill model->mInvBindMatrix
									domSource* source = daeSafeCast<domSource>(input->getSource().getElement());
									if (source)
									{
										domFloat_array* t = source->getFloat_array();
										if (t)
										{
											domListOfFloats& transform = t->getValue();
											S32 count = transform.getCount()/16;

											for (S32 k = 0; k < count; ++k)
											{
												LLMatrix4 mat;

												for (int i = 0; i < 4; i++)
												{
													for(int j = 0; j < 4; j++)
													{
														mat.mMatrix[i][j] = transform[k*16 + i + j*4];
													}
												}

												model->mInvBindMatrix.push_back(mat);
											}
										}
									}
								}
							}

							//We need to construct the alternate bind matrix (which contains the new joint positions)
							//in the same order as they were stored in the joint buffer. The joints associated
							//with the skeleton are not stored in the same order as they are in the exported joint buffer.
							//This remaps the skeletal joints to be in the same order as the joints stored in the model.
							std::vector<std::string> :: const_iterator jointIt  = model->mJointList.begin();							
							const int jointCnt = model->mJointList.size();
							for ( int i=0; i<jointCnt; ++i, ++jointIt )
							{
								std::string lookingForJoint = (*jointIt).c_str();
								//Look for the joint xform that we extracted from the skeleton, using the jointIt as the key
								//and store it in the alternate bind matrix
								if ( jointTransforms.find( lookingForJoint ) != jointTransforms.end() )
								{											
									LLMatrix4 jointTransform = jointTransforms[lookingForJoint];
									LLMatrix4 newInverse = model->mInvBindMatrix[i];
									newInverse.setTranslation( jointTransforms[lookingForJoint].getTranslation() );
									model->mAlternateBindMatrix.push_back( newInverse );								
								}
								else
								{
									llwarns<<"Possibly misnamed/missing joint [" <<lookingForJoint.c_str()<<" ] "<<llendl;
								}
							}   
							
							//grab raw position array
							
							domVertices* verts = mesh->getVertices();
							if (verts)
							{
								domInputLocal_Array& inputs = verts->getInput_array();
								for (size_t i = 0; i < inputs.getCount() && model->mPosition.empty(); ++i)
								{
									if (strcmp(inputs[i]->getSemantic(), COMMON_PROFILE_INPUT_POSITION) == 0)
									{
										domSource* pos_source = daeSafeCast<domSource>(inputs[i]->getSource().getElement());
										if (pos_source)
										{
											domFloat_array* pos_array = pos_source->getFloat_array();
											if (pos_array)
											{
												domListOfFloats& pos = pos_array->getValue();
												
												for (size_t j = 0; j < pos.getCount(); j += 3)
												{
													if (pos.getCount() <= j+2)
													{
														llerrs << "WTF?" << llendl;
													}
													
													LLVector3 v(pos[j], pos[j+1], pos[j+2]);

													//transform from COLLADA space to volume space
													v = v * inverse_normalized_transformation;

													model->mPosition.push_back(v);
												}
											}
										}
									}
								}
							}

							//grab skin weights array
							domSkin::domVertex_weights* weights = skin->getVertex_weights();
							if (weights)
							{
								domInputLocalOffset_Array& inputs = weights->getInput_array();
								domFloat_array* vertex_weights = NULL;
								for (size_t i = 0; i < inputs.getCount(); ++i)
								{
									if (strcmp(inputs[i]->getSemantic(), COMMON_PROFILE_INPUT_WEIGHT) == 0)
									{
										domSource* weight_source = daeSafeCast<domSource>(inputs[i]->getSource().getElement());
										if (weight_source)
										{
											vertex_weights = weight_source->getFloat_array();
										}
									}
								}

								if (vertex_weights)
								{
									domListOfFloats& w = vertex_weights->getValue();
									domListOfUInts& vcount = weights->getVcount()->getValue();
									domListOfInts& v = weights->getV()->getValue();

									U32 c_idx = 0;
									for (size_t vc_idx = 0; vc_idx < vcount.getCount(); ++vc_idx)
									{ //for each vertex
										daeUInt count = vcount[vc_idx];

										//create list of weights that influence this vertex
										LLModel::weight_list weight_list;

										for (daeUInt i = 0; i < count; ++i)
										{ //for each weight
											daeInt joint_idx = v[c_idx++];
											daeInt weight_idx = v[c_idx++];

											if (joint_idx == -1)
											{
												//ignore bindings to bind_shape_matrix
												continue;
											}

											F32 weight_value = w[weight_idx];

											weight_list.push_back(LLModel::JointWeight(joint_idx, weight_value));	
										}

										//sort by joint weight
										std::sort(weight_list.begin(), weight_list.end(), LLModel::CompareWeightGreater());

										std::vector<LLModel::JointWeight> wght;
										
										F32 total = 0.f;

										for (U32 i = 0; i < llmin((U32) 4, (U32) weight_list.size()); ++i)
										{ //take up to 4 most significant weights
											if (weight_list[i].mWeight > 0.f)
											{
												wght.push_back( weight_list[i] );
												total += weight_list[i].mWeight;
											}
										}
										
										F32 scale = 1.f/total;
										if (scale != 1.f)
										{ //normalize weights
											for (U32 i = 0; i < wght.size(); ++i)
											{ 
												wght[i].mWeight *= scale;
											}
										}

										model->mSkinWeights[model->mPosition[vc_idx]] = wght;
									}
									
									//add instance to scene for this model
									
									LLMatrix4 transform;
									std::vector<LLImportMaterial> materials;
									materials.resize(model->getNumVolumeFaces());
									mScene[transform].push_back(LLModelInstance(model, transform, materials));
									stretch_extents(model, transform, mExtents[0], mExtents[1], mFirstTransform);
								}
							}
						}
					}
				}
			}
		}

		daeElement* scene = root->getDescendant("visual_scene");
		if (!scene)
		{
			llwarns << "document has no visual_scene" << llendl;
			return;
		}

		processElement(scene);

		mPreview->loadModelCallback(mLod);
	}
}

void LLModelLoader::processElement(daeElement* element)
{
	LLMatrix4 saved_transform = mTransform;

	domTranslate* translate = daeSafeCast<domTranslate>(element);
	if (translate)
	{
		domFloat3 dom_value = translate->getValue();

		LLMatrix4 translation;
		translation.setTranslation(LLVector3(dom_value[0], dom_value[1], dom_value[2]));
		
		translation *= mTransform;
		mTransform = translation;
	}

	domRotate* rotate = daeSafeCast<domRotate>(element);
	if (rotate)
	{
		domFloat4 dom_value = rotate->getValue();

		LLMatrix4 rotation;
		rotation.initRotTrans(dom_value[3] * DEG_TO_RAD, LLVector3(dom_value[0], dom_value[1], dom_value[2]), LLVector3(0, 0, 0));

		rotation *= mTransform;
		mTransform = rotation;
	}

	domScale* scale = daeSafeCast<domScale>(element);
	if (scale)
	{
		domFloat3 dom_value = scale->getValue();

		LLMatrix4 scaling;
		scaling.initScale(LLVector3(dom_value[0], dom_value[1], dom_value[2]));

		scaling *= mTransform;
		mTransform = scaling;
	}

	domMatrix* matrix = daeSafeCast<domMatrix>(element);
	if (matrix)
	{
		domFloat4x4 dom_value = matrix->getValue();

		LLMatrix4 matrix_transform;

		for (int i = 0; i < 4; i++)
		{
			for(int j = 0; j < 4; j++)
			{
				matrix_transform.mMatrix[i][j] = dom_value[i + j*4];
			}
		}
		
		matrix_transform *= mTransform;
		mTransform = matrix_transform;
	}

	domInstance_geometry* instance_geo = daeSafeCast<domInstance_geometry>(element);
	if (instance_geo)
	{
		domGeometry* geo = daeSafeCast<domGeometry>(instance_geo->getUrl().getElement());
		if (geo)
		{
			domMesh* mesh = daeSafeCast<domMesh>(geo->getDescendant(daeElement::matchType(domMesh::ID())));
			if (mesh)
			{
				LLModel* model = mModel[mesh];
				if (model)
				{
					LLMatrix4 transformation = mTransform;

					std::vector<LLImportMaterial> materials = getMaterials(model, instance_geo);

					// adjust the transformation to compensate for mesh normalization
					LLVector3 mesh_scale_vector;
					LLVector3 mesh_translation_vector;
					model->getNormalizedScaleTranslation(mesh_scale_vector, mesh_translation_vector);

					LLMatrix4 mesh_translation;
					mesh_translation.setTranslation(mesh_translation_vector);
					mesh_translation *= transformation;
					transformation = mesh_translation;
					
					LLMatrix4 mesh_scale;
					mesh_scale.initScale(mesh_scale_vector);
					mesh_scale *= transformation;
					transformation = mesh_scale;
					
					mScene[transformation].push_back(LLModelInstance(model, transformation, materials));

					stretch_extents(model, transformation, mExtents[0], mExtents[1], mFirstTransform);			
				}
			}
		}
	}

	domInstance_node* instance_node = daeSafeCast<domInstance_node>(element);
	if (instance_node)
	{
		daeElement* instance = instance_node->getUrl().getElement();
		if (instance)
		{
			processElement(instance);
		}
	}

	//process children
	daeTArray< daeSmartRef<daeElement> > children = element->getChildren();
	for (S32 i = 0; i < children.getCount(); i++)
	{
		processElement(children[i]);
	}

	domNode* node = daeSafeCast<domNode>(element);
	if (node)
	{ //this element was a node, restore transform before processiing siblings
		mTransform = saved_transform;	
	}
}

std::vector<LLImportMaterial> LLModelLoader::getMaterials(LLModel* model, domInstance_geometry* instance_geo)
{
	std::vector<LLImportMaterial> materials;
	for (int i = 0; i < model->mMaterialList.size(); i++)
	{
		LLImportMaterial import_material;

		domInstance_material* instance_mat = NULL;

		domBind_material::domTechnique_common* technique =
			daeSafeCast<domBind_material::domTechnique_common>(instance_geo->getDescendant(daeElement::matchType(domBind_material::domTechnique_common::ID())));

		if (technique)
		{
			daeTArray< daeSmartRef<domInstance_material> > inst_materials = technique->getChildrenByType<domInstance_material>();
			for (int j = 0; j < inst_materials.getCount(); j++)
			{
				std::string symbol(inst_materials[j]->getSymbol());

				if (symbol == model->mMaterialList[i]) // found the binding
				{
					instance_mat = inst_materials[j];
				}
			}
		}

		if (instance_mat)
		{
			domMaterial* material = daeSafeCast<domMaterial>(instance_mat->getTarget().getElement());
			if (material)
			{
				domInstance_effect* instance_effect =
					daeSafeCast<domInstance_effect>(material->getDescendant(daeElement::matchType(domInstance_effect::ID())));
				if (instance_effect)
				{
					domEffect* effect = daeSafeCast<domEffect>(instance_effect->getUrl().getElement());
					if (effect)
					{
						domProfile_COMMON* profile =
							daeSafeCast<domProfile_COMMON>(effect->getDescendant(daeElement::matchType(domProfile_COMMON::ID())));
						if (profile)
						{
							import_material = profileToMaterial(profile);
						}
					}
				}
			}
		}
		
		materials.push_back(import_material);
	}

	return materials;
}

LLImportMaterial LLModelLoader::profileToMaterial(domProfile_COMMON* material)
{
	LLImportMaterial mat;
	mat.mFullbright = FALSE;

	daeElement* diffuse = material->getDescendant("diffuse");
	if (diffuse)
	{
		domCommon_color_or_texture_type_complexType::domTexture* texture =
			daeSafeCast<domCommon_color_or_texture_type_complexType::domTexture>(diffuse->getDescendant("texture"));
		if (texture)
		{
			domCommon_newparam_type_Array newparams = material->getNewparam_array();
			for (S32 i = 0; i < newparams.getCount(); i++)
			{
				domFx_surface_common* surface = newparams[i]->getSurface();
				if (surface)
				{
					domFx_surface_init_common* init = surface->getFx_surface_init_common();
					if (init)
					{
						domFx_surface_init_from_common_Array init_from = init->getInit_from_array();
						
						if (init_from.getCount() > i)
						{
							domImage* image = daeSafeCast<domImage>(init_from[i]->getValue().getElement());
							if (image)
							{
								// we only support init_from now - embedded data will come later
								domImage::domInit_from* init = image->getInit_from();
								if (init)
								{
									std::string filename = cdom::uriToNativePath(init->getValue().str());
																					
									mat.mDiffuseMap = LLViewerTextureManager::getFetchedTextureFromUrl("file://" + filename, TRUE, LLViewerTexture::BOOST_PREVIEW);
									mat.mDiffuseMap->setLoadedCallback(LLModelPreview::textureLoadedCallback, 0, TRUE, FALSE, this->mPreview, NULL, FALSE);

									mat.mDiffuseMap->forceToSaveRawImage();
									mat.mDiffuseMapFilename = filename;
									mat.mDiffuseMapLabel = getElementLabel(material);
								}
							}
						}
					}
				}
			}
		}
		
		domCommon_color_or_texture_type_complexType::domColor* color =
			daeSafeCast<domCommon_color_or_texture_type_complexType::domColor>(diffuse->getDescendant("color"));
		if (color)
		{
			domFx_color_common domfx_color = color->getValue();
			LLColor4 value = LLColor4(domfx_color[0], domfx_color[1], domfx_color[2], domfx_color[3]);
			mat.mDiffuseColor = value;
		}
	}

	daeElement* emission = material->getDescendant("emission");
	if (emission)
	{
		LLColor4 emission_color = getDaeColor(emission);
		if (((emission_color[0] + emission_color[1] + emission_color[2]) / 3.0) > 0.25)
		{
			mat.mFullbright = TRUE;
		}
	}

	return mat;
}

// try to get a decent label for this element
std::string LLModelLoader::getElementLabel(daeElement *element)
{
	// if we have a name attribute, use it
	std::string name = element->getAttribute("name");
	if (name.length())
	{
		return name;
	}

	// if we have an ID attribute, use it
	if (element->getID())
	{
		return std::string(element->getID());
	}

	// if we have a parent, use it
	daeElement* parent = element->getParent();
	if (parent)
	{
		// if parent has a name, use it
		std::string name = parent->getAttribute("name");
		if (name.length())
		{
			return name;
		}

		// if parent has an ID, use it
		if (parent->getID())
		{
			return std::string(parent->getID());
		}
	}

	// try to use our type
	daeString element_name = element->getElementName();
	if (element_name)
	{
		return std::string(element_name);
	}

	// if all else fails, use "object"
	return std::string("object");
}

LLColor4 LLModelLoader::getDaeColor(daeElement* element)
{
	LLColor4 value;
	domCommon_color_or_texture_type_complexType::domColor* color =
		daeSafeCast<domCommon_color_or_texture_type_complexType::domColor>(element->getDescendant("color"));
	if (color)
	{
		domFx_color_common domfx_color = color->getValue();
		value = LLColor4(domfx_color[0], domfx_color[1], domfx_color[2], domfx_color[3]);
	}

	return value;
}

//-----------------------------------------------------------------------------
// LLModelPreview
//-----------------------------------------------------------------------------

LLModelPreview::LLModelPreview(S32 width, S32 height, LLFloaterModelPreview* fmp) 
: LLViewerDynamicTexture(width, height, 3, ORDER_MIDDLE, FALSE), LLMutex(NULL)
{
	mNeedsUpdate = TRUE;
	mCameraDistance = 0.f;
	mCameraYaw = 0.f;
	mCameraPitch = 0.f;
	mCameraZoom = 1.f;
	mTextureName = 0;
	mPreviewLOD = 3;
	mModelLoader = NULL;
	mDirty = false;

	for (U32 i = 0; i < LLModel::NUM_LODS; i++)
	{
		mLODMode[i] = 1;
		mLimit[i] = 0;
	}

	mLODMode[0] = 0;

	mFMP = fmp;

	glodInit();
}

LLModelPreview::~LLModelPreview()
{
	if (mModelLoader)
	{
		delete mModelLoader;
		mModelLoader = NULL;
	}

	//*HACK : *TODO : turn this back on when we understand why this crashes
	//glodShutdown();
}

U32 LLModelPreview::calcResourceCost()
{
	rebuildUploadData();

	U32 cost = 0;
	std::set<LLModel*> accounted;
	U32 num_points = 0;
	U32 num_hulls = 0;

	F32 debug_scale = mFMP->childGetValue("debug scale").asReal();

	F32 streaming_cost = 0.f;

	for (U32 i = 0; i < mUploadData.size(); ++i)
	{
		LLModelInstance& instance = mUploadData[i];

		if (accounted.find(instance.mModel) == accounted.end())
		{
			accounted.insert(instance.mModel);

			LLModel::convex_hull_decomposition& decomp =
				instance.mLOD[LLModel::LOD_PHYSICS] ?
				instance.mLOD[LLModel::LOD_PHYSICS]->mConvexHullDecomp :
				instance.mModel->mConvexHullDecomp;

			LLSD ret = LLModel::writeModel(
				"",
				instance.mLOD[4],
				instance.mLOD[3], 
				instance.mLOD[2], 
				instance.mLOD[1], 
				instance.mLOD[0],
				decomp,
				mFMP->childGetValue("upload_skin").asBoolean(),
				mFMP->childGetValue("upload_joints").asBoolean(),
				TRUE);
			cost += gMeshRepo.calcResourceCost(ret);
			
			num_hulls += decomp.size();
			for (U32 i = 0; i < decomp.size(); ++i)
			{
				num_points += decomp[i].size();
			}

			//calculate streaming cost
			LLMatrix4 transformation = instance.mTransform;

			LLVector3 position = LLVector3(0, 0, 0) * transformation;

			LLVector3 x_transformed = LLVector3(1, 0, 0) * transformation - position;
			LLVector3 y_transformed = LLVector3(0, 1, 0) * transformation - position;
			LLVector3 z_transformed = LLVector3(0, 0, 1) * transformation - position;
			F32 x_length = x_transformed.normalize();
			F32 y_length = y_transformed.normalize();
			F32 z_length = z_transformed.normalize();
			LLVector3 scale = LLVector3(x_length, y_length, z_length);

			F32 radius = scale.length()*debug_scale;

			streaming_cost += LLMeshRepository::getStreamingCost(ret, radius);
		}
	}

	mFMP->childSetTextArg(info_name[LLModel::LOD_PHYSICS], "[HULLS]", llformat("%d",num_hulls));
	mFMP->childSetTextArg(info_name[LLModel::LOD_PHYSICS], "[POINTS]", llformat("%d",num_points));				
	mFMP->childSetTextArg("streaming cost", "[COST]", llformat("%.3f", streaming_cost)); 
	F32 scale = mFMP->childGetValue("debug scale").asReal()*2.f;
	mFMP->childSetTextArg("dimensions", "[X]", llformat("%.3f", mPreviewScale[0]*scale));
	mFMP->childSetTextArg("dimensions", "[Y]", llformat("%.3f", mPreviewScale[1]*scale));
	mFMP->childSetTextArg("dimensions", "[Z]", llformat("%.3f", mPreviewScale[2]*scale));

	updateStatusMessages();
	
	return cost;
}

void LLModelPreview::rebuildUploadData()
{
	mUploadData.clear();
	mTextureSet.clear();

	//fill uploaddata instance vectors from scene data

	LLSpinCtrl* scale_spinner = mFMP->getChild<LLSpinCtrl>("debug scale");

	if (!scale_spinner)
	{
		llerrs << "floater_model_preview.xml MUST contain debug scale spinner." << llendl;
	}

	F32 scale = scale_spinner->getValue().asReal();

	LLMatrix4 scale_mat;
	scale_mat.initScale(LLVector3(scale, scale, scale));

	F32 max_scale = 0.f;

	for (LLModelLoader::scene::iterator iter = mBaseScene.begin(); iter != mBaseScene.end(); ++iter)
	{ //for each transform in scene
		LLMatrix4 mat = iter->first;

		// compute position
		LLVector3 position = LLVector3(0, 0, 0) * mat;

		// compute scale
		LLVector3 x_transformed = LLVector3(1, 0, 0) * mat - position;
		LLVector3 y_transformed = LLVector3(0, 1, 0) * mat - position;
		LLVector3 z_transformed = LLVector3(0, 0, 1) * mat - position;
		F32 x_length = x_transformed.normalize();
		F32 y_length = y_transformed.normalize();
		F32 z_length = z_transformed.normalize();
		
		max_scale = llmax(llmax(llmax(max_scale, x_length), y_length), z_length);
		
		mat *= scale_mat;
	
		for (LLModelLoader::model_instance_list::iterator model_iter = iter->second.begin(); model_iter != iter->second.end(); ++model_iter)
		{ //for each instance with said transform applied
			LLModelInstance instance = *model_iter;

			LLModel* base_model = instance.mModel;

			S32 idx = 0;
			for (idx = 0; idx < mBaseModel.size(); ++idx)
			{  //find reference instance for this model
				if (mBaseModel[idx] == base_model)
				{
					break;
				}
			}

			for (U32 i = 0; i < LLModel::NUM_LODS; i++)
			{ //fill LOD slots based on reference model index
				if (!mModel[i].empty())
				{
					instance.mLOD[i] = mModel[i][idx];
				}
				else
				{
					instance.mLOD[i] = NULL;
				}
			}

			instance.mTransform = mat;
			mUploadData.push_back(instance);
		}
	}

	F32 max_import_scale = DEFAULT_MAX_PRIM_SCALE/max_scale;

	scale_spinner->setMaxValue(max_import_scale);

	if (max_import_scale < scale)
	{
		scale_spinner->setValue(max_import_scale);
	}
}


void LLModelPreview::clearModel(S32 lod)
{
	if (lod < 0 || lod > LLModel::LOD_PHYSICS)
	{
		return;
	}

	mVertexBuffer[lod].clear();
	mModel[lod].clear();
	mScene[lod].clear();
}

void LLModelPreview::loadModel(std::string filename, S32 lod)
{
	LLMutexLock lock(this);
	
	if (mModelLoader)
	{
		delete mModelLoader;
		mModelLoader = NULL;
	}

	if (filename.empty())
	{
		if (mBaseModel.empty())
		{
			// this is the initial file picking. Close the whole floater
			// if we don't have a base model to show for high LOD.
			mFMP->closeFloater(false);
		}

		mFMP->mLoading = false;
		return;
	}

	if (lod == 3 && !mGroup.empty())
	{
		for (std::map<LLPointer<LLModel>, U32>::iterator iter = mGroup.begin(); iter != mGroup.end(); ++iter)
		{
			glodDeleteGroup(iter->second);
			stop_gloderror();
		}

		for (std::map<LLPointer<LLModel>, U32>::iterator iter = mObject.begin(); iter != mObject.end(); ++iter)
		{
			glodDeleteObject(iter->second);
			stop_gloderror();
		}

		mGroup.clear();
		mObject.clear();
	}

	mModelLoader = new LLModelLoader(filename, lod, this);

	mModelLoader->start();

	mFMP->childSetTextArg("status", "[STATUS]", mFMP->getString("status_reading_file"));

	if (mFMP->childGetValue("description_form").asString().empty())
	{
		std::string name = gDirUtilp->getBaseFileName(filename, true);
		mFMP->childSetValue("description_form", name);
	}

	mFMP->openFloater();
}

void LLModelPreview::clearIncompatible(S32 lod)
{
	for (U32 i = 0; i <= LLModel::LOD_HIGH; i++)
	{ //clear out any entries that aren't compatible with this model
		if (i != lod)
		{
			if (mModel[i].size() != mModel[lod].size())
			{
				mModel[i].clear();
				mScene[i].clear();
				mVertexBuffer[i].clear();

				if (i == LLModel::LOD_HIGH)
				{
					mBaseModel = mModel[lod];
					mBaseScene = mScene[lod];
					mVertexBuffer[5].clear();
				}
			}
		}
	}
}

void LLModelPreview::loadModelCallback(S32 lod)
{ //NOT the main thread
	LLMutexLock lock(this);
	if (!mModelLoader)
	{
		return;
	}

	mModel[lod] = mModelLoader->mModelList;
	mScene[lod] = mModelLoader->mScene;
	mVertexBuffer[lod].clear();
	
	if (lod == LLModel::LOD_PHYSICS)
	{
		mPhysicsMesh.clear();
	}

	setPreviewLOD(lod);
	
	
	if (lod == LLModel::LOD_HIGH)
	{ //save a copy of the highest LOD for automatic LOD manipulation
		mBaseModel = mModel[lod];
		mBaseScene = mScene[lod];
		mVertexBuffer[5].clear();
		//mModel[lod] = NULL;
	}

	clearIncompatible(lod);

	mDirty = true;
	
	resetPreviewTarget();
	
	mFMP->mLoading = FALSE;
	refresh();
}

void LLModelPreview::resetPreviewTarget()
{
	mPreviewTarget = (mModelLoader->mExtents[0] + mModelLoader->mExtents[1]) * 0.5f;
	mPreviewScale = (mModelLoader->mExtents[1] - mModelLoader->mExtents[0]) * 0.5f;
	setPreviewTarget(mPreviewScale.magVec()*2.f);
}

void LLModelPreview::smoothNormals()
{
	S32 which_lod = mPreviewLOD;


	if (which_lod > 4 || which_lod < 0 ||
		mModel[which_lod].empty())
	{
		return;
	}

	F32 angle_cutoff = mFMP->childGetValue("edge threshold").asReal();

	angle_cutoff *= DEG_TO_RAD;

	if (which_lod == 3 && !mBaseModel.empty())
	{
		for (LLModelLoader::model_list::iterator iter = mBaseModel.begin(); iter != mBaseModel.end(); ++iter)
		{
			(*iter)->smoothNormals(angle_cutoff);
		}

		mVertexBuffer[5].clear();
	}

	for (LLModelLoader::model_list::iterator iter = mModel[which_lod].begin(); iter != mModel[which_lod].end(); ++iter)
	{
		(*iter)->smoothNormals(angle_cutoff);
	}
	
	mVertexBuffer[which_lod].clear();
	refresh();

}

void LLModelPreview::consolidate()
{
	std::map<LLImportMaterial, std::vector<LLModelInstance> > composite;

	LLMatrix4 identity;

	//bake out each node in current scene to composite
	for (LLModelLoader::scene::iterator iter = mScene[mPreviewLOD].begin(); iter != mScene[mPreviewLOD].end(); ++iter)
	{ //for each transform in current scene
		LLMatrix4 mat = iter->first;
		glh::matrix4f inv_trans = glh::matrix4f((F32*) mat.mMatrix).inverse().transpose();
		LLMatrix4 norm_mat(inv_trans.m);

		for (LLModelLoader::model_instance_list::iterator model_iter = iter->second.begin(); model_iter != iter->second.end(); ++model_iter)
		{ //for each instance with that transform
			LLModelInstance& source_instance = *model_iter;
			LLModel* source = source_instance.mModel;
			
			if (!validate_model(source))
			{
				llerrs << "Invalid model found!" << llendl;
			}

			for (S32 i = 0; i < source->getNumVolumeFaces(); ++i)
			{ //for each face in instance
				const LLVolumeFace& src_face = source->getVolumeFace(i);
				LLImportMaterial& source_material = source_instance.mMaterial[i];

				//get model in composite that is composite for this material
				LLModel* model = NULL;

				if (composite.find(source_material) != composite.end())
				{
					model = composite[source_material].rbegin()->mModel;
					if (model->getVolumeFace(0).mNumVertices + src_face.mNumVertices > 65535)
					{
						model = NULL;
					}
				}

				if (model == NULL)
				{  //no model found, make new model
					std::vector<LLImportMaterial> materials;
					materials.push_back(source_material);
					LLVolumeParams volume_params;
					volume_params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
					model = new LLModel(volume_params, 0.f);
					model->mLabel = source->mLabel;
					model->setNumVolumeFaces(0);
					composite[source_material].push_back(LLModelInstance(model, identity, materials));
				}
			
				model->appendFace(src_face, source->mMaterialList[i], mat, norm_mat);
			}
		}
	}


	//condense composite into as few LLModel instances as possible
	LLModelLoader::model_list new_model;
	std::vector<LLModelInstance> instance_list;
	
	LLVolumeParams volume_params;
	volume_params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);

	std::vector<LLImportMaterial> empty_material;
	LLModelInstance cur_instance(new LLModel(volume_params, 0.f), identity, empty_material);
	cur_instance.mModel->setNumVolumeFaces(0);

	BOOL first_transform = TRUE;

	LLModelLoader::scene new_scene;
	LLVector3 min,max;

	for (std::map<LLImportMaterial, std::vector<LLModelInstance> >::iterator iter = composite.begin();
			iter != composite.end();
			++iter)
	{
		std::map<LLImportMaterial, std::vector<LLModelInstance> >::iterator next_iter = iter; ++next_iter;
		
		for (std::vector<LLModelInstance>::iterator instance_iter = iter->second.begin(); 
				instance_iter != iter->second.end();
				++instance_iter)
		{
			LLModel* source = instance_iter->mModel;

			if (instance_iter->mMaterial.size() != 1)
			{
				llerrs << "WTF?" << llendl;
			}

			if (source->getNumVolumeFaces() != 1)
			{
				llerrs << "WTF?" << llendl;
			}

			if (source->mMaterialList.size() != 1)
			{
				llerrs << "WTF?" << llendl;
			}

			cur_instance.mModel->addFace(source->getVolumeFace(0));
			cur_instance.mMaterial.push_back(instance_iter->mMaterial[0]);
			cur_instance.mModel->mMaterialList.push_back(source->mMaterialList[0]);

			BOOL last_model = FALSE;
		
			std::vector<LLModelInstance>::iterator next_instance = instance_iter; ++next_instance;

			if (next_iter == composite.end() &&
				next_instance == iter->second.end())
			{
				last_model = TRUE;
			}

			if (last_model || cur_instance.mModel->getNumVolumeFaces() >= MAX_MODEL_FACES)
			{
				cur_instance.mModel->mLabel = source->mLabel;

				cur_instance.mModel->optimizeVolumeFaces();
				cur_instance.mModel->normalizeVolumeFaces();

				if (!validate_model(cur_instance.mModel))
				{
					llerrs << "Invalid model detected." << llendl;
				}

				new_model.push_back(cur_instance.mModel);

				LLMatrix4 transformation = LLMatrix4();

				// adjust the transformation to compensate for mesh normalization
				LLVector3 mesh_scale_vector;
				LLVector3 mesh_translation_vector;
				cur_instance.mModel->getNormalizedScaleTranslation(mesh_scale_vector, mesh_translation_vector);

				LLMatrix4 mesh_translation;
				mesh_translation.setTranslation(mesh_translation_vector);
				mesh_translation *= transformation;
				transformation = mesh_translation;
				
				LLMatrix4 mesh_scale;
				mesh_scale.initScale(mesh_scale_vector);
				mesh_scale *= transformation;
				transformation = mesh_scale;
							
				cur_instance.mTransform = transformation;

				new_scene[transformation].push_back(cur_instance);
				stretch_extents(cur_instance.mModel, transformation, min, max, first_transform);

				if (!last_model)
				{
					cur_instance = LLModelInstance(new LLModel(volume_params, 0.f), identity, empty_material);
					cur_instance.mModel->setNumVolumeFaces(0);
				}
			}
		}
	}
		
	mScene[mPreviewLOD] = new_scene;
	mModel[mPreviewLOD] = new_model;
	mVertexBuffer[mPreviewLOD].clear();

	if (mPreviewLOD == LLModel::LOD_HIGH)
	{
		mBaseScene = new_scene;
		mBaseModel = new_model;
		mVertexBuffer[5].clear();
	}

	mPreviewTarget = (min+max)*0.5f;
	mPreviewScale = (max-min)*0.5f;
	setPreviewTarget(mPreviewScale.magVec()*2.f);

	clearIncompatible(mPreviewLOD);

	mResourceCost = calcResourceCost();
	refresh();
}

void LLModelPreview::scrubMaterials()
{
	for (LLModelLoader::scene::iterator iter = mScene[mPreviewLOD].begin(); iter != mScene[mPreviewLOD].end(); ++iter)
	{ //for each transform in current scene
		for (LLModelLoader::model_instance_list::iterator model_iter = iter->second.begin(); model_iter != iter->second.end(); ++model_iter)
		{ //for each instance with that transform
			LLModelInstance& source_instance = *model_iter;
			LLModel* source = source_instance.mModel;
			
			for (S32 i = 0; i < source->getNumVolumeFaces(); ++i)
			{ //for each face in instance
				LLImportMaterial& source_material = source_instance.mMaterial[i];

				//clear material info
				source_material.mDiffuseColor = LLColor4(1,1,1,1);
				source_material.mDiffuseMap = NULL;
				source_material.mDiffuseMapFilename.clear();
				source_material.mDiffuseMapLabel.clear();
				source_material.mFullbright = false;
			}
		}
	}


	mVertexBuffer[mPreviewLOD].clear();

	if (mPreviewLOD == LLModel::LOD_HIGH)
	{
		mBaseScene = mScene[mPreviewLOD];
		mBaseModel = mModel[mPreviewLOD];
		mVertexBuffer[5].clear();
	}

	mResourceCost = calcResourceCost();
	refresh();
}

bool LLModelPreview::containsRiggedAsset( void )
{
	//loop through the models and determine if any of them contained a rigged asset, and if so
	//return true.
	//This is used to cleanup the joint positions after a preview.
	for (LLModelLoader::model_list::iterator iter = mBaseModel.begin(); iter != mBaseModel.end(); ++iter)
	{
		LLModel* pModel = *iter;
		if ( pModel->mAlternateBindMatrix.size() > 0 )
		{
			return true;
		}
	}
	return false;
}
void LLModelPreview::genLODs(S32 which_lod)
{
	if (mBaseModel.empty())
	{
		return;
	}

	if (which_lod == LLModel::LOD_PHYSICS)
	{ //clear physics mesh map
		mPhysicsMesh.clear();
	}

	LLVertexBuffer::unbind();

	stop_gloderror();
	static U32 cur_name = 1;

	S32 limit = -1;

	if (which_lod != -1)
	{
		limit = mLimit[which_lod];
	}

	U32 triangle_count = 0;

	for (LLModelLoader::model_list::iterator iter = mBaseModel.begin(); iter != mBaseModel.end(); ++iter)
	{
		LLModel* mdl = *iter;
		for (S32 i = 0; i < mdl->getNumVolumeFaces(); ++i)
		{
			triangle_count += mdl->getVolumeFace(i).mNumIndices/3;
		}
	}

	U32 base_triangle_count = triangle_count;

	U32 type_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL | LLVertexBuffer::MAP_TEXCOORD0;

	if (mGroup[mBaseModel[0]] == 0)
	{ //clear LOD maps
		mGroup.clear();
		mObject.clear();
		mPercentage.clear();
		mPatch.clear();
	}

	for (LLModelLoader::model_list::iterator iter = mBaseModel.begin(); iter != mBaseModel.end(); ++iter)
	{ //build GLOD objects for each model in base model list
		LLModel* mdl = *iter;
		if (mGroup[mdl] == 0)
		{
			mGroup[mdl] = cur_name++;
			mObject[mdl] = cur_name++;

			glodNewGroup(mGroup[mdl]);
			stop_gloderror();

			glodGroupParameteri(mGroup[mdl], GLOD_ADAPT_MODE, GLOD_TRIANGLE_BUDGET);
			stop_gloderror();		

			glodGroupParameteri(mGroup[mdl], GLOD_ERROR_MODE, GLOD_OBJECT_SPACE_ERROR);
			stop_gloderror();

			glodGroupParameterf(mGroup[mdl], GLOD_OBJECT_SPACE_ERROR_THRESHOLD, 0.025f);
			stop_gloderror();

			glodNewObject(mObject[mdl], mGroup[mdl], GLOD_DISCRETE);
			stop_gloderror();

			if (iter == mBaseModel.begin() && !mdl->mSkinWeights.empty())
			{ //regenerate vertex buffer for skinned models to prevent animation feedback during LOD generation
				mVertexBuffer[5].clear();
			}

			if (mVertexBuffer[5].empty())
			{
				genBuffers(5, false);
			}

			U32 tri_count = 0;
			for (U32 i = 0; i < mVertexBuffer[5][mdl].size(); ++i)
			{
				mVertexBuffer[5][mdl][i]->setBuffer(type_mask);
				U32 num_indices = mVertexBuffer[5][mdl][i]->getNumIndices();
				if (num_indices > 2)
				{
					glodInsertElements(mObject[mdl], i, GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, mVertexBuffer[5][mdl][i]->getIndicesPointer(), 0, 0.f);
				}
				tri_count += num_indices/3;
				stop_gloderror();
			}

			//store what percentage of total model (in terms of triangle count) this model makes up
			mPercentage[mdl] = (F32) tri_count / (F32) base_triangle_count;

			//build glodobject
			glodBuildObject(mObject[mdl]);
			if (stop_gloderror())
			{
				glodDeleteGroup(mGroup[mdl]);
				stop_gloderror();
				glodDeleteObject(mObject[mdl]);
				stop_gloderror();

				mGroup[mdl] = 0;
				mObject[mdl] = 0;

				if (which_lod == -1)
				{
					mModel[LLModel::LOD_HIGH] = mBaseModel;
				}

				return;
			}

		}
		
		//generating LODs for all entries, or this entry has a triangle budget
		glodGroupParameteri(mGroup[mdl], GLOD_ADAPT_MODE, GLOD_TRIANGLE_BUDGET);
		stop_gloderror();		
		
		glodGroupParameterf(mGroup[mdl], GLOD_OBJECT_SPACE_ERROR_THRESHOLD, 0.025f);
		stop_gloderror();
	}


	S32 start = LLModel::LOD_HIGH;
	S32 end = 0;

	if (which_lod != -1)
	{
		start = end = which_lod;
	}
	
	
	std::string combo_name[] = 
	{
		"lowest detail combo",
		"low detail combo",
		"medium detail combo",
		"high detail combo",
		"physics detail combo"
	};

	std::string limit_name[] =
	{
		"lowest limit",
		"low limit",
		"medium limit",
		"high limit",
		"physics limit"
	};

	for (S32 lod = start; lod >= end; --lod)
	{
		if (which_lod == -1)
		{
			if (lod < start)
			{
				triangle_count /= 3;
			}
		}
		else
		{
			triangle_count = limit;
		}

		LLComboBox* combo_box = mFMP->findChild<LLComboBox>(combo_name[lod]);
		combo_box->setCurrentByIndex(2);
	
		LLSpinCtrl* lim = mFMP->getChild<LLSpinCtrl>(limit_name[lod], TRUE);
		lim->setMaxValue(base_triangle_count);
		lim->setVisible(true);
					
		mModel[lod].clear();
		mModel[lod].resize(mBaseModel.size());
		mVertexBuffer[lod].clear();

		U32 actual_tris = 0;
		U32 actual_verts = 0;
		U32 submeshes = 0;

		for (U32 mdl_idx = 0; mdl_idx < mBaseModel.size(); ++mdl_idx)
		{ 
			LLModel* base = mBaseModel[mdl_idx];

			U32 target_count = U32(mPercentage[base]*triangle_count);

			if (target_count < 4)
			{ 
				target_count = 4;
			}

			glodGroupParameteri(mGroup[base], GLOD_MAX_TRIANGLES, target_count);
			stop_gloderror();
						
			glodAdaptGroup(mGroup[base]);
			stop_gloderror();

			GLint patch_count = 0;
			glodGetObjectParameteriv(mObject[base], GLOD_NUM_PATCHES, &patch_count);
			stop_gloderror();

			LLVolumeParams volume_params;
			volume_params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
			mModel[lod][mdl_idx] = new LLModel(volume_params, 0.f);

			GLint* sizes = new GLint[patch_count*2];
			glodGetObjectParameteriv(mObject[base], GLOD_PATCH_SIZES, sizes);
			stop_gloderror();

			GLint* names = new GLint[patch_count];
			glodGetObjectParameteriv(mObject[base], GLOD_PATCH_NAMES, names);
			stop_gloderror();

			mModel[lod][mdl_idx]->setNumVolumeFaces(patch_count);
			
			LLModel* target_model = mModel[lod][mdl_idx];

			for (GLint i = 0; i < patch_count; ++i)
			{
				LLPointer<LLVertexBuffer> buff = new LLVertexBuffer(type_mask, 0);
				
				if (sizes[i*2+1] > 0 && sizes[i*2] > 0)
				{
					buff->allocateBuffer(sizes[i*2+1], sizes[i*2], true);
					buff->setBuffer(type_mask);
					glodFillElements(mObject[base], names[i], GL_UNSIGNED_SHORT, buff->getIndicesPointer());
					stop_gloderror();
				}
				else
				{ //this face was eliminated, create a dummy triangle (one vertex, 3 indices, all 0)
					buff->allocateBuffer(1, 3, true);
					memset(buff->getMappedData(), 0, buff->getSize());
					memset(buff->getIndicesPointer(), 0, buff->getIndicesSize());
				}
				
				buff->validateRange(0, buff->getNumVerts()-1, buff->getNumIndices(), 0);

				LLStrider<LLVector3> pos;
				LLStrider<LLVector3> norm;
				LLStrider<LLVector2> tc;
				LLStrider<U16> index;

				buff->getVertexStrider(pos);
				buff->getNormalStrider(norm);
				buff->getTexCoord0Strider(tc);
				buff->getIndexStrider(index);


				target_model->setVolumeFaceData(names[i], pos, norm, tc, index, buff->getNumVerts(), buff->getNumIndices());
				actual_tris += buff->getNumIndices()/3;
				actual_verts += buff->getNumVerts();
				++submeshes;

				if (!validate_face(target_model->getVolumeFace(names[i])))
				{
					llerrs << "Invalid face generated during LOD generation." << llendl;
				}
			}

			//blind copy skin weights and just take closest skin weight to point on
			//decimated mesh for now (auto-generating LODs with skin weights is still a bit
			//of an open problem).
			target_model->mPosition = base->mPosition;
			target_model->mSkinWeights = base->mSkinWeights;
			target_model->mJointMap = base->mJointMap;
			target_model->mJointList = base->mJointList;
			target_model->mInvBindMatrix = base->mInvBindMatrix;
			target_model->mBindShapeMatrix = base->mBindShapeMatrix;
			target_model->mAlternateBindMatrix = base->mAlternateBindMatrix;
			//copy material list
			target_model->mMaterialList = base->mMaterialList;

			if (!validate_model(target_model))
			{
				llerrs << "Invalid model generated when creating LODs" << llendl;
			}

			delete [] sizes;
			delete [] names;
		}

		//rebuild scene based on mBaseScene
		mScene[lod].clear();
		mScene[lod] = mBaseScene;

		for (U32 i = 0; i < mBaseModel.size(); ++i)
		{
			LLModel* mdl = mBaseModel[i];
			LLModel* target = mModel[lod][i];
			if (target)
			{
				for (LLModelLoader::scene::iterator iter = mScene[lod].begin(); iter != mScene[lod].end(); ++iter)
				{
					for (U32 j = 0; j < iter->second.size(); ++j)
					{
						if (iter->second[j].mModel == mdl)
						{
							iter->second[j].mModel = target;
						}
					}
				}
			}
		}
	}

	mResourceCost = calcResourceCost();

	/*if (which_lod == -1 && mScene[LLModel::LOD_PHYSICS].empty())
	{ //build physics scene
		mScene[LLModel::LOD_PHYSICS] = mScene[LLModel::LOD_LOW];
		mModel[LLModel::LOD_PHYSICS] = mModel[LLModel::LOD_LOW];

		for (U32 i = 1; i < mModel[LLModel::LOD_PHYSICS].size(); ++i)
		{
			mPhysicsQ.push(mModel[LLModel::LOD_PHYSICS][i]);
		}
	}*/
}

void LLModelPreview::updateStatusMessages()
{
	//triangle/vertex/submesh count for each mesh asset for each lod
	std::vector<S32> tris[LLModel::NUM_LODS];
	std::vector<S32> verts[LLModel::NUM_LODS];
	std::vector<S32> submeshes[LLModel::NUM_LODS];
	
	//total triangle/vertex/submesh count for each lod
	S32 total_tris[LLModel::NUM_LODS];
	S32 total_verts[LLModel::NUM_LODS];
	S32 total_submeshes[LLModel::NUM_LODS];

	for (S32 lod = 0; lod < LLModel::NUM_LODS; ++lod)
	{
		//initialize total for this lod to 0
		total_tris[lod] = total_verts[lod] = total_submeshes[lod] = 0;

		for (U32 i = 0; i < mModel[lod].size(); ++i)
		{ //for each model in the lod
			S32 cur_tris = 0;
			S32 cur_verts = 0;
			S32 cur_submeshes = mModel[lod][i]->getNumVolumeFaces();

			for (S32 j = 0; j < cur_submeshes; ++j)
			{ //for each submesh (face), add triangles and vertices to current total
				const LLVolumeFace& face = mModel[lod][i]->getVolumeFace(j);
				cur_tris += face.mNumIndices/3;
				cur_verts += face.mNumVertices;
			}

			//add this model to the lod total
			total_tris[lod] += cur_tris;
			total_verts[lod] += cur_verts;
			total_submeshes[lod] += cur_submeshes;

			//store this model's counts to asset data
			tris[lod].push_back(cur_tris);
			verts[lod].push_back(cur_verts);
			submeshes[lod].push_back(cur_submeshes);
		}
	}
	

	std::string upload_message;

	mFMP->childSetTextArg(info_name[LLModel::LOD_PHYSICS], "[TRIANGLES]", llformat("%d", total_tris[LLModel::LOD_PHYSICS]));

	for (S32 lod = 0; lod <= LLModel::LOD_HIGH; ++lod)
	{
		mFMP->childSetTextArg(info_name[lod], "[TRIANGLES]", llformat("%d", total_tris[lod]));
		mFMP->childSetTextArg(info_name[lod], "[VERTICES]", llformat("%d", total_verts[lod]));
		mFMP->childSetTextArg(info_name[lod], "[SUBMESHES]", llformat("%d", total_submeshes[lod]));

		std::string message = "good";
		
		const U32 lod_high = LLModel::LOD_HIGH;

		if (lod != lod_high)
		{
			if (total_submeshes[lod] && total_submeshes[lod] != total_submeshes[lod_high])
			{
				message = "mesh_mismatch";
				upload_message = "bad_lod";
			}
			else if (!tris[lod].empty() && tris[lod].size() != tris[lod_high].size())
			{
				message = "model_mismatch";
				upload_message = "bad_lod";
			}
			else if (!verts[lod].empty())
			{
				for (U32 i = 0; i < verts[lod].size(); ++i)
				{
					S32 max_verts = verts[lod+1][i];

					if (verts[lod][i] > max_verts)
					{
						message = "too_heavy";
						upload_message = "bad_lod";
					}
				}
			}
		}

		mFMP->childSetTextArg(info_name[lod], "[MESSAGE]", mFMP->getString(message));
	}

	if (upload_message.empty())
	{
		mFMP->childSetTextArg("upload_message", "[MESSAGE]", std::string(""));
		mFMP->childEnable("ok_btn");
	}
	else
	{
		mFMP->childSetTextArg("upload_message", "[MESSAGE]", mFMP->getString(upload_message));
		mFMP->childDisable("ok_btn");
	}
}

void LLModelPreview::setPreviewTarget(F32 distance)
{ 
	mCameraDistance = distance;
	mCameraZoom = 1.f;
	mCameraPitch = 0.f;
	mCameraYaw = 0.f;
	mCameraOffset.clearVec();
}

void LLModelPreview::clearBuffers()
{
	for (U32 i = 0; i < 6; i++)
	{
		mVertexBuffer[i].clear();
	}
}

void LLModelPreview::genBuffers(S32 lod, bool avatar_preview)
{
	U32 tri_count = 0;
	U32 vertex_count = 0;
	U32 mesh_count = 0;

	LLModelLoader::model_list* model = NULL;

	if (lod < 0 || lod > 4)
	{
		model = &mBaseModel;
		lod = 5;
	}
	else
	{
		model = &(mModel[lod]);
	}

	if (!mVertexBuffer[lod].empty())
	{
		mVertexBuffer[lod].clear();
	}

	mVertexBuffer[lod].clear();

	LLModelLoader::model_list::iterator base_iter = mBaseModel.begin();

	for (LLModelLoader::model_list::iterator iter = model->begin(); iter != model->end(); ++iter)
	{
		LLModel* mdl = *iter;
		if (!mdl)
		{
			continue;
		}

		LLModel* base_mdl = *base_iter;
		base_iter++;

		for (S32 i = 0; i < mdl->getNumVolumeFaces(); ++i)
		{
			const LLVolumeFace &vf = mdl->getVolumeFace(i);
			U32 num_vertices = vf.mNumVertices;
			U32 num_indices = vf.mNumIndices;

			if (!num_vertices || ! num_indices)
			{
				continue;
			}

			LLVertexBuffer* vb = NULL;
			
			bool skinned = avatar_preview && !mdl->mSkinWeights.empty();

			U32 mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL | LLVertexBuffer::MAP_TEXCOORD0;
			
			if (skinned)
			{
				mask |= LLVertexBuffer::MAP_WEIGHT4;
			}

			vb = new LLVertexBuffer(mask, 0);
			
			vb->allocateBuffer(num_vertices, num_indices, TRUE);

			LLStrider<LLVector3> vertex_strider;
			LLStrider<LLVector3> normal_strider;
			LLStrider<LLVector2> tc_strider;
			LLStrider<U16> index_strider;
			LLStrider<LLVector4> weights_strider;

			vb->getVertexStrider(vertex_strider);
			vb->getNormalStrider(normal_strider);
			vb->getTexCoord0Strider(tc_strider);
			vb->getIndexStrider(index_strider);

			if (skinned)
			{
				vb->getWeight4Strider(weights_strider);
			}
			
			LLVector4a::memcpyNonAliased16((F32*) vertex_strider.get(), (F32*) vf.mPositions, num_vertices*4*sizeof(F32));
			LLVector4a::memcpyNonAliased16((F32*) tc_strider.get(), (F32*) vf.mTexCoords, num_vertices*2*sizeof(F32));
			LLVector4a::memcpyNonAliased16((F32*) normal_strider.get(), (F32*) vf.mNormals, num_vertices*4*sizeof(F32));

			if (skinned)
			{
				for (U32 i = 0; i < num_vertices; i++)
				{
					//find closest weight to vf.mVertices[i].mPosition
					LLVector3 pos(vf.mPositions[i].getF32ptr());

					const LLModel::weight_list& weight_list = base_mdl->getJointInfluences(pos);

					LLVector4 w(0,0,0,0);
					if (weight_list.size() > 4)
					{
						llerrs << "WTF?" << llendl;
					}

					for (U32 i = 0; i < weight_list.size(); ++i)
					{
						F32 wght = llmin(weight_list[i].mWeight, 0.999999f);
						F32 joint = (F32) weight_list[i].mJointIdx;
						w.mV[i] = joint + wght;
					}
					
					*(weights_strider++) = w;
				}
			}

			// build indices
			for (U32 i = 0; i < num_indices; i++)
			{
				*(index_strider++) = vf.mIndices[i];
			}

			mVertexBuffer[lod][mdl].push_back(vb);

			vertex_count += num_vertices;
			tri_count += num_indices/3;
			++mesh_count;

		}
	}
}

void LLModelPreview::update()
{
	if (mDirty)
	{
		mDirty = false;
		mResourceCost = calcResourceCost();
	}
}

//-----------------------------------------------------------------------------
// render()
//-----------------------------------------------------------------------------
BOOL LLModelPreview::render()
{
	LLMutexLock lock(this);
	mNeedsUpdate = FALSE;

	S32 width = getWidth();
	S32 height = getHeight();

	LLGLSUIDefault def;
	LLGLDisable no_blend(GL_BLEND);
	LLGLEnable cull(GL_CULL_FACE);
	LLGLDepthTest depth(GL_TRUE);
	LLGLDisable fog(GL_FOG);

	glMatrixMode(GL_PROJECTION);
	gGL.pushMatrix();
	glLoadIdentity();
	glOrtho(0.0f, width, 0.0f, height, -1.0f, 1.0f);

	glMatrixMode(GL_MODELVIEW);
	gGL.pushMatrix();
	glLoadIdentity();
		
	gGL.color4f(0.15f, 0.2f, 0.3f, 1.f);

	gl_rect_2d_simple( width, height );

	bool avatar_preview = false;
	bool upload_skin = mFMP->childGetValue("upload_skin").asBoolean();
	bool upload_joints = mFMP->childGetValue("upload_joints").asBoolean();

	for (LLModelLoader::scene::iterator iter = mScene[mPreviewLOD].begin(); iter != mScene[mPreviewLOD].end(); ++iter)
	{
		for (LLModelLoader::model_instance_list::iterator model_iter = iter->second.begin(); model_iter != iter->second.end(); ++model_iter)
		{
			LLModelInstance& instance = *model_iter;
			LLModel* model = instance.mModel;
			if (!model->mSkinWeights.empty())
			{
				avatar_preview = true;
			}
		}
	}

	if (upload_skin && !avatar_preview)
	{
		mFMP->childSetValue("upload_skin", false);
		upload_skin = false;
	}

	if (!upload_skin && upload_joints)
	{
		mFMP->childSetValue("upload_joints", false);
		upload_joints = false;
	}

	if (!avatar_preview)
	{
		mFMP->childDisable("upload_skin");
	}
	else
	{
		mFMP->childEnable("upload_skin");
	}

	if (!upload_skin)
	{
		mFMP->childDisable("upload_joints");
	}
	else
	{
		mFMP->childEnable("upload_joints");
	}

	avatar_preview = avatar_preview && upload_skin;

		
	mFMP->childSetEnabled("consolidate", !avatar_preview);
	
	F32 explode = mFMP->mDecompFloater ? mFMP->mDecompFloater->childGetValue("explode").asReal() : 0.f;

	glMatrixMode(GL_PROJECTION);
	gGL.popMatrix();

	glMatrixMode(GL_MODELVIEW);
	gGL.popMatrix();

	glClear(GL_DEPTH_BUFFER_BIT);

	LLViewerCamera::getInstance()->setAspect((F32) width / height );
	LLViewerCamera::getInstance()->setView(LLViewerCamera::getInstance()->getDefaultFOV() / mCameraZoom);

	LLVector3 target_pos = mPreviewTarget;
	LLVector3 offset = mCameraOffset;

	F32 z_near = llmax(mCameraDistance-mPreviewScale.magVec(), 0.001f);
	F32 z_far = mCameraDistance+mPreviewScale.magVec();

	if (avatar_preview)
	{
		target_pos = gAgentAvatarp->getPositionAgent();
		z_near = 0.01f;
		z_far = 1024.f;
		mCameraDistance = 16.f;

		//render avatar previews every frame
		refresh();
	}

	LLQuaternion camera_rot = LLQuaternion(mCameraPitch, LLVector3::y_axis) * 
		LLQuaternion(mCameraYaw, LLVector3::z_axis);

	LLQuaternion av_rot = camera_rot;
	LLViewerCamera::getInstance()->setOriginAndLookAt(
		target_pos + ((LLVector3(mCameraDistance, 0.f, 0.f) + offset) * av_rot),		// camera
		LLVector3::z_axis,																	// up
		target_pos);											// point of interest

	
	LLViewerCamera::getInstance()->setPerspective(FALSE, mOrigin.mX, mOrigin.mY, width, height, FALSE, z_near, z_far);

	stop_glerror();

	gPipeline.enableLightsAvatar();

	gGL.pushMatrix();
	const F32 BRIGHTNESS = 0.9f;
	gGL.color3f(BRIGHTNESS, BRIGHTNESS, BRIGHTNESS);
	
	LLGLEnable normalize(GL_NORMALIZE);

	if (!mBaseModel.empty() && mVertexBuffer[5].empty())
	{
		genBuffers(-1, avatar_preview);
		//genBuffers(3);
		//genLODs();
	}

	bool physics = (mPreviewLOD == LLModel::LOD_PHYSICS);

	S32 physics_idx = -1;

	bool render_mesh = true;
	bool render_hull = false;

	if (physics && mFMP->mDecompFloater)
	{
		physics_idx = mFMP->mDecompFloater->childGetValue("model").asInteger();
		render_mesh = mFMP->mDecompFloater->childGetValue("render_mesh").asBoolean();
		render_hull = mFMP->mDecompFloater->childGetValue("render_hull").asBoolean();
	}

	if (!mModel[mPreviewLOD].empty())
	{
		if (mVertexBuffer[mPreviewLOD].empty())
		{
			genBuffers(mPreviewLOD, avatar_preview);
		}

		if (!avatar_preview)
		{
			//for (LLModelLoader::scene::iterator iter = mScene[mPreviewLOD].begin(); iter != mScene[mPreviewLOD].end(); ++iter)
			for (LLMeshUploadThread::instance_list::iterator iter = mUploadData.begin(); iter != mUploadData.end(); ++iter)
			{
				LLModelInstance& instance = *iter;

				gGL.pushMatrix();
				LLMatrix4 mat = instance.mTransform;

				glMultMatrixf((GLfloat*) mat.mMatrix);				
				
				//for (LLModelLoader::model_instance_list::iterator model_iter = iter->second.begin(); model_iter != iter->second.end(); ++model_iter)
				{
					//LLModelInstance& instance = *model_iter;
					LLModel* model = instance.mLOD[mPreviewLOD];

					if (!model)
					{
						continue;
					}

					//if (instance.mTransform != mat)
					//{
					//	llerrs << "WTF?" << llendl;
					//}

					if (render_mesh)
					{
						for (U32 i = 0; i < mVertexBuffer[mPreviewLOD][model].size(); ++i)
						{
							LLVertexBuffer* buffer = mVertexBuffer[mPreviewLOD][model][i];

							buffer->setBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL | LLVertexBuffer::MAP_TEXCOORD0);
							if (physics)
							{
								if (physics_idx > -1 && model == mModel[mPreviewLOD][physics_idx])
								{
									glColor4f(1,0,0,1);
								}
								else
								{
									glColor4f(0.75f, 0.75f, 0.75f, 1.f);
								}
							}
							else
							{
								glColor4fv(instance.mMaterial[i].mDiffuseColor.mV);
								if (i < instance.mMaterial.size() && instance.mMaterial[i].mDiffuseMap.notNull())
								{
									gGL.getTexUnit(0)->bind(instance.mMaterial[i].mDiffuseMap, true);
									if (instance.mMaterial[i].mDiffuseMap->getDiscardLevel() > -1)
									{
										mTextureSet.insert(instance.mMaterial[i].mDiffuseMap);
									}
								}
							}

							buffer->drawRange(LLRender::TRIANGLES, 0, buffer->getNumVerts()-1, buffer->getNumIndices(), 0);
							gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
							glColor3f(0.4f, 0.4f, 0.4f);

							if (mFMP->childGetValue("show edges").asBoolean())
							{
								glLineWidth(3.f);
								glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
								buffer->drawRange(LLRender::TRIANGLES, 0, buffer->getNumVerts()-1, buffer->getNumIndices(), 0);
								glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
								glLineWidth(1.f);
							}
						}
					}

					if (render_hull)
					{
						LLPhysicsDecomp* decomp = gMeshRepo.mDecompThread;
						if (decomp)
						{
							LLMutexLock(decomp->mMutex);
												
							std::map<LLPointer<LLModel>, std::vector<LLPointer<LLVertexBuffer> > >::iterator iter = 
								mPhysicsMesh.find(model);
							if (iter != mPhysicsMesh.end())
							{
								for (U32 i = 0; i < iter->second.size(); ++i)
								{
									if (explode > 0.f)
									{
										gGL.pushMatrix();

										LLVector3 offset = model->mHullCenter[i]-model->mCenterOfHullCenters;
										offset *= explode;

										gGL.translatef(offset.mV[0], offset.mV[1], offset.mV[2]);
									}

									static std::vector<LLColor4U> hull_colors;

									if (i+1 >= hull_colors.size())
									{
										hull_colors.push_back(LLColor4U(rand()%128+127, rand()%128+127, rand()%128+127, 255));
									}

									LLVertexBuffer* buff = iter->second[i];
									if (buff)
									{
										buff->setBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL);			

										glColor4ubv(hull_colors[i].mV);
										buff->drawArrays(LLRender::TRIANGLES, 0, buff->getNumVerts());
									
										if (mFMP->childGetValue("show edges").asBoolean())
										{
											glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
											glLineWidth(3.f);
											glColor4ub(hull_colors[i].mV[0]/2, hull_colors[i].mV[1]/2, hull_colors[i].mV[2]/2, 255);
											buff->drawArrays(LLRender::TRIANGLES, 0, buff->getNumVerts());
											glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
											glLineWidth(1.f);
										}	
									}

									if (explode > 0.f)
									{
										gGL.popMatrix();
									}
								}
							}
						}	

						//mFMP->childSetTextArg(info_name[LLModel::LOD_PHYSICS], "[HULLS]", llformat("%d",decomp->mHulls.size()));
						//mFMP->childSetTextArg(info_name[LLModel::LOD_PHYSICS], "[POINTS]", llformat("%d",decomp->mTotalPoints));				
					}
				}

				gGL.popMatrix();
			}

			if (physics)
			{
				mPreviewLOD = LLModel::LOD_PHYSICS;
			}
		}
		else
		{
			LLVOAvatarSelf* avatar = gAgentAvatarp;
			target_pos = avatar->getPositionAgent();

			LLViewerCamera::getInstance()->setOriginAndLookAt(
				target_pos + ((LLVector3(mCameraDistance, 0.f, 0.f) + offset) * av_rot),		// camera
				LLVector3::z_axis,																	// up
				target_pos);											// point of interest

			avatar->renderCollisionVolumes();

			for (LLModelLoader::scene::iterator iter = mScene[mPreviewLOD].begin(); iter != mScene[mPreviewLOD].end(); ++iter)
			{
				for (LLModelLoader::model_instance_list::iterator model_iter = iter->second.begin(); model_iter != iter->second.end(); ++model_iter)
				{
					LLModelInstance& instance = *model_iter;
					LLModel* model = instance.mModel;

					if (!model->mSkinWeights.empty())
					{
						for (U32 i = 0; i < mVertexBuffer[mPreviewLOD][model].size(); ++i)
						{
							LLVertexBuffer* buffer = mVertexBuffer[mPreviewLOD][model][i];

							const LLVolumeFace& face = model->getVolumeFace(i);

							LLStrider<LLVector3> position;
							buffer->getVertexStrider(position);
							
							LLStrider<LLVector4> weight;
							buffer->getWeight4Strider(weight);
							
							//quick 'n dirty software vertex skinning

							//build matrix palette
							LLMatrix4 mat[64];
							for (U32 j = 0; j < model->mJointList.size(); ++j)
							{
								LLJoint* joint = avatar->getJoint(model->mJointList[j]);
								if (joint)
								{
									mat[j] = model->mInvBindMatrix[j];
									mat[j] *= joint->getWorldMatrix();
								}
							}

							for (U32 j = 0; j < buffer->getRequestedVerts(); ++j)
							{
								LLMatrix4 final_mat;
								final_mat.mMatrix[0][0] = final_mat.mMatrix[1][1] = final_mat.mMatrix[2][2] = final_mat.mMatrix[3][3] = 0.f;

								LLVector4 wght;
								S32 idx[4];

								F32 scale = 0.f;
								for (U32 k = 0; k < 4; k++)
								{
									F32 w = weight[j].mV[k];

									idx[k] = (S32) floorf(w);
									wght.mV[k] = w - floorf(w);
									scale += wght.mV[k];
								}

								wght *= 1.f/scale;						

								for (U32 k = 0; k < 4; k++)
								{
									F32* src = (F32*) mat[idx[k]].mMatrix;
									F32* dst = (F32*) final_mat.mMatrix;

									F32 w = wght.mV[k];

									for (U32 l = 0; l < 16; l++)
									{
										dst[l] += src[l]*w;
									}
								}

								//VECTORIZE THIS
								LLVector3 v(face.mPositions[j].getF32ptr());
								
								v = v * model->mBindShapeMatrix;
								v = v * final_mat;

								position[j] = v;
							}

							buffer->setBuffer(LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL | LLVertexBuffer::MAP_TEXCOORD0);
							glColor4fv(instance.mMaterial[i].mDiffuseColor.mV);
							gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
							buffer->draw(LLRender::TRIANGLES, buffer->getNumIndices(), 0);
							glColor3f(0.4f, 0.4f, 0.4f);

							if (mFMP->childGetValue("show edges").asBoolean())
							{
								glLineWidth(3.f);
								glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
								buffer->draw(LLRender::TRIANGLES, buffer->getNumIndices(), 0);
								glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
								glLineWidth(1.f);
							}
						}
					}
				}
			}
		}
	}

	gGL.popMatrix();
		
	return TRUE;
}

//-----------------------------------------------------------------------------
// refresh()
//-----------------------------------------------------------------------------
void LLModelPreview::refresh()
{ 
	mNeedsUpdate = TRUE; 
}

//-----------------------------------------------------------------------------
// rotate()
//-----------------------------------------------------------------------------
void LLModelPreview::rotate(F32 yaw_radians, F32 pitch_radians)
{
	mCameraYaw = mCameraYaw + yaw_radians;

	mCameraPitch = llclamp(mCameraPitch + pitch_radians, F_PI_BY_TWO * -0.8f, F_PI_BY_TWO * 0.8f);
}

//-----------------------------------------------------------------------------
// zoom()
//-----------------------------------------------------------------------------
void LLModelPreview::zoom(F32 zoom_amt)
{
	F32 new_zoom = mCameraZoom+zoom_amt;
		
	mCameraZoom	= llclamp(new_zoom, 1.f, 10.f);
}

void LLModelPreview::pan(F32 right, F32 up)
{
	mCameraOffset.mV[VY] = llclamp(mCameraOffset.mV[VY] + right * mCameraDistance / mCameraZoom, -1.f, 1.f);
	mCameraOffset.mV[VZ] = llclamp(mCameraOffset.mV[VZ] + up * mCameraDistance / mCameraZoom, -1.f, 1.f);
}

void LLModelPreview::setPreviewLOD(S32 lod)
{
	mPreviewLOD = llclamp(lod, 0, 4);
	refresh();
}

//static 
void LLFloaterModelPreview::onBrowseHighLOD(void* data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) data;
	mp->loadModel(3);
}

//static 
void LLFloaterModelPreview::onBrowseMediumLOD(void* data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) data;
	mp->loadModel(2);
}

//static 
void LLFloaterModelPreview::onBrowseLowLOD(void* data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) data;
	mp->loadModel(1);
}

//static 
void LLFloaterModelPreview::onBrowseLowestLOD(void* data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) data;
	mp->loadModel(0);
}

//static
void LLFloaterModelPreview::onUpload(void* user_data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) user_data;

	if (mp->mDecompFloater)
	{
		mp->mDecompFloater->closeFloater();
	}

	mp->mModelPreview->rebuildUploadData();
		
	gMeshRepo.uploadModel(mp->mModelPreview->mUploadData, mp->mModelPreview->mPreviewScale, 
		mp->childGetValue("upload_textures").asBoolean(), mp->childGetValue("upload_skin"), mp->childGetValue("upload_joints"));

	mp->closeFloater(false);
}

//static 
void LLFloaterModelPreview::onConsolidate(void* user_data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) user_data;
	mp->mModelPreview->consolidate();
}

//static 
void LLFloaterModelPreview::onScrubMaterials(void* user_data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) user_data;
	mp->mModelPreview->scrubMaterials();
}

//static 
void LLFloaterModelPreview::onDecompose(void* user_data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) user_data;
	mp->showDecompFloater();
}

//static 
void LLFloaterModelPreview::refresh(LLUICtrl* ctrl, void* user_data)
{
	LLFloaterModelPreview* mp = (LLFloaterModelPreview*) user_data;
	mp->mModelPreview->refresh();
}

void LLFloaterModelPreview::updateResourceCost()
{
	U32 cost = mModelPreview->mResourceCost;
	childSetLabelArg("ok_btn", "[AMOUNT]", llformat("%d",cost));
}

//static
void LLModelPreview::textureLoadedCallback( BOOL success, LLViewerFetchedTexture *src_vi, LLImageRaw* src, LLImageRaw* src_aux, S32 discard_level, BOOL final, void* userdata )
{
	LLModelPreview* preview = (LLModelPreview*) userdata;
	preview->refresh();
}

LLFloaterModelPreview::DecompRequest::DecompRequest(const std::string& stage, LLModel* mdl)
{
	mStage = stage;
	mContinue = 1;
	mModel = mdl;
	mParams = sInstance->mDecompParams;

	//copy out positions and indices
	if (mdl)
	{
		U16 index_offset = 0;

		mPositions.clear();
		mIndices.clear();
			
		//queue up vertex positions and indices
		for (S32 i = 0; i < mdl->getNumVolumeFaces(); ++i)
		{
			const LLVolumeFace& face = mdl->getVolumeFace(i);
			if (mPositions.size() + face.mNumVertices > 65535)
			{
				continue;
			}

			for (U32 j = 0; j < face.mNumVertices; ++j)
			{
				mPositions.push_back(LLVector3(face.mPositions[j].getF32ptr()));
			}

			for (U32 j = 0; j < face.mNumIndices; ++j)
			{
				mIndices.push_back(face.mIndices[j]+index_offset);
			}

			index_offset += face.mNumVertices;
		}
	}
}

S32 LLFloaterModelPreview::DecompRequest::statusCallback(const char* status, S32 p1, S32 p2)
{
	setStatusMessage(llformat("%s: %d/%d", status, p1, p2));
	return mContinue;
}

void LLFloaterModelPreview::DecompRequest::completed()
{
	mModel->setConvexHullDecomposition(mHull);
	
	if (sInstance) 
	{ 
		if (sInstance->mModelPreview)
		{
			sInstance->mModelPreview->mPhysicsMesh[mModel] = mHullMesh;
			sInstance->mModelPreview->mDirty = true;
			LLFloaterModelPreview::sInstance->mModelPreview->refresh();
		}
		
		sInstance->mCurRequest = NULL;
	}
}


