#include "SeamsEasyNode.h"
#include "StitchEasyNode.h"
#include "SNode.h"
#include "SPlane.h"

#include <maya\MFnNumericAttribute.h>
#include <maya\MFnTypedAttribute.h>
#include <maya\MFnCompoundAttribute.h>
#include <maya\MFnComponentListData.h>
#include <maya\MFnMesh.h>
#include <maya\MFnMeshData.h>
#include <maya\MFnSingleIndexedComponent.h>
#include <maya\MGlobal.h>
#include <maya\MPointArray.h>
#include <maya\MFloatPointArray.h>
#include <maya\MPlugArray.h>
#include <maya\MFnSet.h>
#include <maya\MFnEnumAttribute.h>
#include <maya\MFnUnitAttribute.h>
#include <maya\MRampAttribute.h>
#include <maya\MDistance.h>
#include <maya\MDGModifier.h>
#include <maya\MDagModifier.h>
#include <maya\MItDependencyGraph.h>
#include <maya\MAngle.h>
#include <maya\MPxManipContainer.h>

MTypeId SeamsEasyNode::id(0x00127891);

MObject SeamsEasyNode::aOutMesh;
MObject SeamsEasyNode::aOutStitchLines;

MObject SeamsEasyNode::aInMesh;
MObject SeamsEasyNode::aSelectedEdges;

MObject SeamsEasyNode::aExtrudeAllBoundaries;
MObject SeamsEasyNode::aExtrudeThickness;
MObject SeamsEasyNode::aExtrudeDivisions;
MObject SeamsEasyNode::aGap;

MObject SeamsEasyNode::aProfileMode;
MObject SeamsEasyNode::aSymmetry;

// Manual profile
MObject SeamsEasyNode::aOffsetA;
MObject SeamsEasyNode::aOffsetADistance;
MObject SeamsEasyNode::aOffsetADepth;
MObject SeamsEasyNode::aOffsetAStitch;

MObject SeamsEasyNode::aOffsetB;
MObject SeamsEasyNode::aOffsetBDistance;
MObject SeamsEasyNode::aOffsetBDepth;
MObject SeamsEasyNode::aOffsetBStitch;

MObject SeamsEasyNode::aDistanceMultiplier;
MObject SeamsEasyNode::aDepthMultiplier;

// Curve profile
MObject SeamsEasyNode::aProfileAWidth;
MObject SeamsEasyNode::aProfileADepth;
MObject SeamsEasyNode::aProfileASubdivs;
MObject SeamsEasyNode::aProfileACurve;

MObject SeamsEasyNode::aProfileBWidth;
MObject SeamsEasyNode::aProfileBDepth;
MObject SeamsEasyNode::aProfileBSubdivs;
MObject SeamsEasyNode::aProfileBCurve;

MObject SeamsEasyNode::aHardEdgeAngle;

OffsetParams::Compare OffsetParams::compare = OffsetParams::kDistance;

SeamsEasyNode::SeamsEasyNode(){
}

void SeamsEasyNode::postConstructor(){
	callbackIds.append(MNodeMessage::addAttributeChangedCallback(thisMObject(), attrChanged, NULL));

	// Set default profile curve shape
	MRampAttribute rAttrA(thisMObject(), aProfileACurve);
	MRampAttribute rAttrB(thisMObject(), aProfileBCurve);
	rAttrA.setValueAtIndex(1, 0);
	rAttrB.setValueAtIndex(1, 0);

	MFloatArray positions, values;
	MIntArray interpolation;

	positions.append(0.5f);
	values.append(0.85f);
	interpolation.append(MRampAttribute::kSpline);
	
	positions.append(0.85f);
	values.append(0.5f);
	interpolation.append(MRampAttribute::kSpline);
	
	positions.append(1.0f);
	values.append(0.0f);
	interpolation.append(MRampAttribute::kSpline);
	
	rAttrA.addEntries(positions, values, interpolation);
	rAttrB.addEntries(positions, values, interpolation);
}

SeamsEasyNode::~SeamsEasyNode(){
	MMessage::removeCallbacks(callbackIds);
}

void *SeamsEasyNode::creator()
{
	return new SeamsEasyNode;
}

MStatus SeamsEasyNode::initialize()
{
	MStatus status;

	MFnNumericAttribute nAttr;
	MFnTypedAttribute tAttr;
	MFnCompoundAttribute cAttr;
	MFnEnumAttribute eAttr;
	MFnUnitAttribute uAttr;
	MRampAttribute rAttr;

	aOutMesh = tAttr.create("outMesh", "outMesh", MFnData::kMesh, &status);
	tAttr.setWritable(false);
	addAttribute(aOutMesh);

	aOutStitchLines = tAttr.create("stitchLines", "stitchLines", MFnData::kComponentList, &status);
	tAttr.setWritable(false);
	addAttribute(aOutStitchLines);

	aInMesh = tAttr.create("inMesh", "inMesh", MFnData::kMesh, &status);
	addAttribute(aInMesh);
	attributeAffects(aInMesh, aOutMesh);
	attributeAffects(aInMesh, aOutStitchLines);

	aSelectedEdges = tAttr.create("seamLines", "seamLines", MFnData::kComponentList, &status);
	addAttribute(aSelectedEdges);
	attributeAffects(aSelectedEdges, aOutMesh);
	attributeAffects(aSelectedEdges, aOutStitchLines);

	// Extrude attributes /////////////////////////////////////////////////////////////////////////
	aExtrudeAllBoundaries = nAttr.create("extrudeAllBoundaries", "extrudeAllBoundaries", MFnNumericData::kBoolean, 0, &status);
	addAttribute(aExtrudeAllBoundaries);
	attributeAffects(aExtrudeAllBoundaries, aOutMesh);

	aExtrudeThickness = uAttr.create("thickness", "thickness", MFnUnitAttribute::kDistance, 1.0, &status);
	uAttr.setSoftMin(0);
	uAttr.setSoftMax(10);
	addAttribute(aExtrudeThickness);
	attributeAffects(aExtrudeThickness, aOutMesh);

	aExtrudeDivisions = nAttr.create("divisions", "divisions", MFnNumericData::kInt, 0, &status);
	nAttr.setMin(0);
	nAttr.setSoftMax(10);
	addAttribute(aExtrudeDivisions);
	attributeAffects(aExtrudeDivisions, aOutMesh);

	aGap = uAttr.create("gap", "gap", MFnUnitAttribute::kDistance, 0.0, &status);
	uAttr.setMin(0);
	uAttr.setSoftMax(10);
	addAttribute(aGap);
	attributeAffects(aGap, aOutMesh);

	// Profile attributes //////////////////////////////////////////////////////////////////////////
	aProfileMode = eAttr.create("profileMode", "profileMode");
	eAttr.addField("Manual", 0);
	eAttr.addField("Curve", 1);
	addAttribute(aProfileMode);
	attributeAffects(aProfileMode, aOutMesh);
	attributeAffects(aProfileMode, aOutStitchLines);

	// Profile A
	aProfileAWidth = uAttr.create("profileWidthA", "profileWidthA", MFnUnitAttribute::kDistance, 1.0);
	uAttr.setMin(0);
	uAttr.setSoftMax(1);
	addAttribute(aProfileAWidth);
	attributeAffects(aProfileAWidth, aOutMesh);

	aProfileADepth = uAttr.create("profileDepthA", "profileDepthA", MFnUnitAttribute::kDistance, -1.0);
	uAttr.setSoftMin(-1);
	uAttr.setSoftMax(1);
	addAttribute(aProfileADepth);
	attributeAffects(aProfileADepth, aOutMesh);

	aProfileASubdivs = nAttr.create("profileSubdivsA", "profileSubdivsA", MFnNumericData::kInt, 4);
	nAttr.setMin(0);
	nAttr.setSoftMax(10);
	addAttribute(aProfileASubdivs);
	attributeAffects(aProfileASubdivs, aOutMesh);
	
	aProfileACurve = rAttr.createCurveRamp("profileCurveA", "profileCurveA");
	addAttribute(aProfileACurve);
	attributeAffects(aProfileACurve, aOutMesh);

	// Profile B
	aProfileBWidth = uAttr.create("profileWidthB", "profileWidthB", MFnUnitAttribute::kDistance, 1.0);
	uAttr.setMin(0);
	uAttr.setSoftMax(1);
	addAttribute(aProfileBWidth);
	attributeAffects(aProfileBWidth, aOutMesh);

	aProfileBDepth = uAttr.create("profileDepthB", "profileDepthB", MFnUnitAttribute::kDistance, -1.0);
	uAttr.setSoftMin(-1);
	uAttr.setSoftMax(1);
	addAttribute(aProfileBDepth);
	attributeAffects(aProfileBDepth, aOutMesh);

	aProfileBSubdivs = nAttr.create("profileSubdivsB", "profileSubdivsB", MFnNumericData::kInt, 4);
	nAttr.setMin(0);
	nAttr.setSoftMax(10);
	addAttribute(aProfileBSubdivs);
	attributeAffects(aProfileBSubdivs, aOutMesh);

	aProfileBCurve = rAttr.createCurveRamp("profileCurveB", "profileCurveB");
	addAttribute(aProfileBCurve);
	attributeAffects(aProfileBCurve, aOutMesh);
	
	// Offset attributes //////////////////////////////////////////////////////////////////////////

	aSymmetry = nAttr.create("symmetry", "symmetry", MFnNumericData::kBoolean, 1, &status);
	addAttribute(aSymmetry);
	attributeAffects(aSymmetry, aOutMesh);

	aDistanceMultiplier = nAttr.create("distanceMultiplier", "distanceMultiplier", MFnNumericData::kFloat, 1.0, &status);
	nAttr.setMin(0);
	nAttr.setSoftMax(10);
	addAttribute(aDistanceMultiplier);
	attributeAffects(aDistanceMultiplier, aOutMesh);
	attributeAffects(aDistanceMultiplier, aOutStitchLines);

	aDepthMultiplier = nAttr.create("depthMultiplier", "depthMultiplier", MFnNumericData::kFloat, 1.0, &status);
	nAttr.setMin(0);
	nAttr.setSoftMax(10);
	addAttribute(aDepthMultiplier);
	attributeAffects(aDepthMultiplier, aOutMesh);
	attributeAffects(aDepthMultiplier, aOutStitchLines);
	
	// OffsetA attributes
	aOffsetA = cAttr.create("offsetA", "offsetA", &status);
	cAttr.setArray(true);

	aOffsetADistance = uAttr.create("distanceA", "distanceA", MFnUnitAttribute::kDistance, 0.0, &status);
	uAttr.setMin(0);
	uAttr.setSoftMax(10);
	addAttribute(aOffsetADistance);
	attributeAffects(aOffsetADistance, aOutMesh);
	attributeAffects(aOffsetADistance, aOutStitchLines);
	cAttr.addChild(aOffsetADistance);

	aOffsetADepth = uAttr.create("depthA", "depthA", MFnUnitAttribute::kDistance, 0.0, &status);
	uAttr.setSoftMin(-1);
	uAttr.setSoftMax(1);
	addAttribute(aOffsetADepth);
	attributeAffects(aOffsetADepth, aOutMesh);
	cAttr.addChild(aOffsetADepth);

	aOffsetAStitch = nAttr.create("stitchA", "stitchA", MFnNumericData::kBoolean, 0, &status);
	addAttribute(aOffsetAStitch);
	attributeAffects(aOffsetAStitch, aOutMesh);
	attributeAffects(aOffsetAStitch, aOutStitchLines);
	cAttr.addChild(aOffsetAStitch);

	addAttribute(aOffsetA);
	attributeAffects(aOffsetA, aOutMesh);
	attributeAffects(aOffsetA, aOutStitchLines);

	// OffsetB attributes
	aOffsetB = cAttr.create("offsetB", "offsetB", &status);
	cAttr.setArray(true);

	aOffsetBDistance = uAttr.create("distanceB", "distanceB", MFnUnitAttribute::kDistance, 0.0, &status);
	uAttr.setMin(0);
	uAttr.setSoftMax(10);
	addAttribute(aOffsetBDistance);
	attributeAffects(aOffsetBDistance, aOutMesh);
	attributeAffects(aOffsetBDistance, aOutStitchLines);
	cAttr.addChild(aOffsetBDistance);

	aOffsetBDepth = uAttr.create("depthB", "depthB", MFnUnitAttribute::kDistance, 0.0, &status);
	uAttr.setSoftMin(-1);
	uAttr.setSoftMax(1);
	addAttribute(aOffsetBDepth);
	attributeAffects(aOffsetBDepth, aOutMesh);
	cAttr.addChild(aOffsetBDepth);

	aOffsetBStitch = nAttr.create("stitchB", "stitchB", MFnNumericData::kBoolean, 0, &status);
	addAttribute(aOffsetBStitch);
	attributeAffects(aOffsetBStitch, aOutMesh);
	attributeAffects(aOffsetBStitch, aOutStitchLines);
	cAttr.addChild(aOffsetBStitch);

	addAttribute(aOffsetB);
	attributeAffects(aOffsetB, aOutMesh);
	attributeAffects(aOffsetB, aOutStitchLines);

	aHardEdgeAngle = uAttr.create("hardEdgeAngle", "hardEdgeAngle", MFnUnitAttribute::kAngle, M_PI/2, &status);
	nAttr.setMin(0);
	nAttr.setMax(180);
	addAttribute(aHardEdgeAngle);
	attributeAffects(aHardEdgeAngle, aOutMesh);

	MPxManipContainer::addToManipConnectTable(id);

	return MS::kSuccess;
}

MStatus SeamsEasyNode::setDependentsDirty(const MPlug &plug, MPlugArray &affected) {
	if (aInMesh == plug)
		dirtyMesh = true;

	if (aSelectedEdges == plug)
		dirtyComponent = true;

	if (aExtrudeAllBoundaries == plug ||
		aExtrudeThickness == plug ||
		aExtrudeDivisions == plug)
		dirtyExtrude = true;

	if (aGap == plug ||
		aProfileMode == plug ||
		aSymmetry == plug ||

		aProfileAWidth == plug ||
		aProfileADepth == plug ||
		aProfileASubdivs == plug ||
		aProfileACurve == plug.parent().array() ||

		aProfileBWidth == plug ||
		aProfileBDepth == plug ||
		aProfileBSubdivs == plug ||
		aProfileBCurve == plug.parent().array() ||

		aOffsetA == plug ||
		aOffsetADepth == plug ||
		aOffsetADistance == plug ||
		aOffsetAStitch == plug ||

		aOffsetB == plug ||
		aOffsetBDepth == plug ||
		aOffsetBDistance == plug ||
		aOffsetBStitch == plug ||

		aDepthMultiplier == plug||
		aDistanceMultiplier == plug)
		dirtyProfile = true;

	return MS::kSuccess;
}

MStatus SeamsEasyNode::compute(const MPlug &plug, MDataBlock &dataBlock) {
	MStatus status;

	if (plug != aOutMesh)
		return MS::kUnknownParameter;

	// Load attribs ///////////////////////////////////////////////////////////////////////////////
	if (dirtyMesh || m_baseMesh.isNull()) {
		MObject sourceMesh = dataBlock.inputValue(aInMesh).asMesh();
		if (m_baseMesh.isNull() || !SMesh::isEquivalent(sourceMesh, m_baseMesh.getObject())) {
			dirtyBaseMesh = true;
			dirtyComponent = true;
			m_baseMesh = SSeamMesh(sourceMesh);
		}
		else
			m_baseMesh.updateMesh(sourceMesh);
	}
	if (dirtyComponent || m_component.isNull()) {
		dirtyBaseMesh = true;
		dirtyComponent = false;
		MObject oSeamsEasyData = dataBlock.inputValue(aSelectedEdges).data();
		MFnComponentListData compListData(oSeamsEasyData, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		m_component = compListData[0];
		m_baseMesh.setActiveEdges(m_component);
	}
	if (m_component.apiType() != MFn::kMeshEdgeComponent || m_baseMesh.isNull())
		return MS::kInvalidParameter;

	// Load numeric attribs ///////////////////////////////////////////////////////////////////////
	bool extrudeAll = dataBlock.inputValue(aExtrudeAllBoundaries).asBool();
	float thickness = (float)dataBlock.inputValue(aExtrudeThickness).asDistance().as(MDistance::internalUnit());
	unsigned int divisions = dataBlock.inputValue(aExtrudeDivisions).asInt();
	double hardEdgeAngle = dataBlock.inputValue(aHardEdgeAngle).asAngle().as(MAngle::internalUnit());
	bool symmetry = dataBlock.inputValue(aSymmetry).asBool();

	// Load profile settings //////////////////////////////////////////////////////////////////////
	std::set <OffsetParams> offsetParamsA;
	std::set <OffsetParams> offsetParamsB;
	OffsetParams::compare = OffsetParams::kDistance;

	dirtyProfile = true;
	if (dirtyProfile) {

		dirtyProfile = false;
		dirtyProfileMesh = true;

		int profileMode = dataBlock.inputValue(aProfileMode).asInt();

		switch (profileMode)
		{
		case 1: {
			// Profile curve mode
			// Side A
			int subdivisionsA = dataBlock.inputValue(aProfileASubdivs).asInt();
			float widthA = (float)dataBlock.inputValue(aProfileAWidth).asDistance().as(MDistance::internalUnit());
			float depthA = (float)dataBlock.inputValue(aProfileADepth).asDistance().as(MDistance::internalUnit());
			loadProfileCurveSetting(aProfileACurve, widthA, depthA, subdivisionsA, offsetParamsA);

			// Side B
			if (symmetry)
				offsetParamsB = offsetParamsA;
			else {
				int subdivisionsB = dataBlock.inputValue(aProfileBSubdivs).asInt();
				float widthB = (float)dataBlock.inputValue(aProfileBWidth).asDistance().as(MDistance::internalUnit());
				float depthB = (float)dataBlock.inputValue(aProfileBDepth).asDistance().as(MDistance::internalUnit());
				loadProfileCurveSetting(aProfileBCurve, widthB, depthB, subdivisionsB, offsetParamsB);
			}
		}
		default: { // manual mode
			float distanceMultiplier = dataBlock.inputValue(aDistanceMultiplier).asFloat();
			float depthMultiplier = dataBlock.inputValue(aDepthMultiplier).asFloat();
			
			// Side A
			MArrayDataHandle hOffsetArray = dataBlock.inputArrayValue(aOffsetA);
			for (unsigned int i = 0; i < hOffsetArray.elementCount(); i++) {
				OffsetParams newParam;

				hOffsetArray.jumpToArrayElement(i);
				MDataHandle hOffsetElement = hOffsetArray.inputValue();

				newParam.index = i;
				newParam.distance = (float)hOffsetElement.child(aOffsetADistance).asDistance().as(MDistance::internalUnit())*distanceMultiplier;
				newParam.depth = (float)hOffsetElement.child(aOffsetADepth).asDistance().as(MDistance::internalUnit())*depthMultiplier;
				newParam.stitch = hOffsetElement.child(aOffsetAStitch).asBool();

				offsetParamsA.insert(newParam);
			}

			// Side B
			if (symmetry)
				offsetParamsB = offsetParamsA;
			else{
				hOffsetArray = dataBlock.inputArrayValue(aOffsetB);
				for (unsigned int i = 0; i < hOffsetArray.elementCount(); i++) {
					OffsetParams newParam;

					hOffsetArray.jumpToArrayElement(i);
					MDataHandle hOffsetElement = hOffsetArray.inputValue();

					newParam.index = i;
					newParam.distance = (float)hOffsetElement.child(aOffsetBDistance).asDistance().as(MDistance::internalUnit())*distanceMultiplier;
					newParam.depth = (float)hOffsetElement.child(aOffsetBDepth).asDistance().as(MDistance::internalUnit())*depthMultiplier;
					newParam.stitch = hOffsetElement.child(aOffsetBStitch).asBool();

					offsetParamsB.insert(newParam);
				}
			}

			break;
		}
		}
	}

	if (offsetParamsA.size() == 0 || offsetParamsA.begin()->distance != 0){
		OffsetParams newParam(0, 0, 0, dataBlock.inputArrayValue(aOffsetA).elementCount());
		offsetParamsA.insert(newParam);
	}
	if (offsetParamsB.size() == 0 || offsetParamsB.begin()->distance != 0) {
		OffsetParams newParam(0, 0, 0, dataBlock.inputArrayValue(aOffsetB).elementCount());
		offsetParamsB.insert(newParam);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Update base mesh
	if (dirtyMesh || dirtyBaseMesh) {
		dirtyMesh = false;
		dirtyProfileMesh = true;

		if (!dirtyBaseMesh) {
			status = m_workMesh.updateMesh(m_baseMesh.getObject());
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		else {
			dirtyBaseMesh = false;

			MIntArray splitlineArray;
			m_baseMesh.getActiveEdges(splitlineArray);

			// Create base mesh
			m_workMesh = SSeamMesh(m_baseMesh);
			status = m_workMesh.detachEdges(splitlineArray);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = m_workMesh.transferEdges(m_baseMesh.getObject(), splitlineArray);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}
	// Update profile mesh
	dirtyProfileMesh = true;
	if (dirtyProfileMesh) {
		dirtyProfileMesh = false;

		float gap = (float)dataBlock.inputValue(aGap).asDistance().as(MDistance::internalUnit());

		m_profileMesh = SSeamMesh(m_workMesh);
		
		// Componend to be passed to stitch node
		MFnComponentListData compListData;
		MObject compList = compListData.create();
		MFnSingleIndexedComponent stitchCompData;
		MObject stitchComponent = stitchCompData.create(MFn::kMeshEdgeComponent);

		// Insert new loops
		m_loopEdgesA.clear();
		m_loopEdgesB.clear();

		std::vector <SEdgeLoop> *baseLoopsPtr = m_baseMesh.activeLoopsPtr();
		std::vector <SEdgeLoop> *activeLoopsPtr = m_profileMesh.activeLoopsPtr();

		std::map <unsigned int, unsigned int> parentEdgeMap;
		m_profileMesh.getEdgeMap(parentEdgeMap);

		for (auto &loop : *activeLoopsPtr) {
			bool sideA = true;
			unsigned parentEdge = parentEdgeMap[loop[0]];

			unsigned i = 0;
			for (auto &baseLoop : *baseLoopsPtr) {
				if (baseLoop.contains(parentEdge)) {
					MVector pEdgeVector = m_baseMesh.getEdgeVector(parentEdge);
					MVector edgeVector = m_profileMesh.getEdgeVector(loop[0]);

					sideA = (pEdgeVector*edgeVector >= 0) ? true : false;

					if(baseLoop.isReversed())
						sideA = (sideA) ? false : true;

					break;
				}
			}

			std::set <OffsetParams> offsetParams = (sideA) ? offsetParamsA : offsetParamsB;

			for (auto param = offsetParams.rbegin(); param != offsetParams.rend(); param++) {
				m_profileMesh.getActiveEdges((sideA)?m_loopEdgesA[param->index]: m_loopEdgesB[param->index]);
				if (param->stitch)
					stitchCompData.addElements((sideA)?m_loopEdgesA[param->index]:m_loopEdgesB[param->index]);

				float offsetDistance = param->distance + gap / 2;
				bool createPolygon = (0 == param->distance && param == --offsetParams.rend()) ? false : true;

				status = m_profileMesh.offsetEdgeloop(loop, offsetDistance, createPolygon);
				CHECK_MSTATUS_AND_RETURN_IT(status);
			}
		}

		status = compListData.add(stitchComponent);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dataBlock.outputValue(aOutStitchLines).set(compList);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		// Pull loop vertices
		for (auto param : offsetParamsA){
			MIntArray loopVertices;
			status = m_profileMesh.getEdgeVertices(m_loopEdgesA[param.index], loopVertices);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = m_profileMesh.pullVertices(loopVertices, param.depth);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		for (auto param : offsetParamsB) {
			MIntArray loopVertices;
			status = m_profileMesh.getEdgeVertices(m_loopEdgesB[param.index], loopVertices);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = m_profileMesh.pullVertices(loopVertices, param.depth);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}

	SSeamMesh extrudeMesh(m_profileMesh);
	if (thickness != 0) {
		MIntArray edgesToExtrude;
		if (extrudeAll)
			extrudeMesh.getBoundaryEdges(edgesToExtrude);
		else
			extrudeMesh.getActiveEdges(edgesToExtrude);

		status = extrudeMesh.extrudeEdges(edgesToExtrude, thickness, divisions);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	for (auto &loop : m_loopEdgesA) {
		status = extrudeMesh.setHardEdges(loop.second, hardEdgeAngle);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	for (auto &loop : m_loopEdgesB) {
		status = extrudeMesh.setHardEdges(loop.second, hardEdgeAngle);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	status = dataBlock.outputValue(aOutMesh).set(extrudeMesh.getObject());
	CHECK_MSTATUS_AND_RETURN_IT(status);
	dataBlock.setClean(aOutMesh);

	return MS::kSuccess;
}

float SeamsEasyNode::remap(const float srcMin, const float srcMax, const float trgMin, const float trgMax, float value) {
	float srcDomain = srcMax - srcMin;
	float trgDomain = trgMax - trgMin;

	if (srcDomain == 0)
		return value;

	return trgMin + (value - srcMin) * trgDomain / srcDomain;
}

void SeamsEasyNode::attrChanged(MNodeMessage::AttributeMessage msg, MPlug &plug, MPlug &otherPlug, void *data) {
	if (plug != aOffsetAStitch)
		return;

	MPlug pOffset = plug.parent().array();

	bool shouldConnect = false;
	for (unsigned int i = 0; i < pOffset.numElements(); i++)
		if (pOffset.elementByPhysicalIndex(i).child(aOffsetAStitch).asBool()) {
			shouldConnect = true;
			break;
		}

	MFnDependencyNode fnNode(plug.node());
	MPlugArray targets;
	bool isConnected = fnNode.findPlug(aOutStitchLines).connectedTo(targets, false, true);

	MDGModifier dgMod;
	MDagModifier dagMod;
	if (!isConnected && shouldConnect)
		dgMod.commandToExecute("stitchEasy -sn "+fnNode.name()+" "+fnNode.name()+"Stitch");
	if (isConnected && !shouldConnect) {
		for (unsigned int t = 0; t < targets.length(); t++) {
			MFnDependencyNode fnStitchNode(targets[t].node());
			if (fnStitchNode.typeId() != StitchEasyNode::id)
				continue;

			MPlugArray outMeshes;
			fnStitchNode.findPlug(StitchEasyNode::aOutMesh).connectedTo(outMeshes, false, true);
			for (unsigned int m = 0; m < outMeshes.length(); m++)
				dagMod.deleteNode(outMeshes[m].node());

			dgMod.deleteNode(targets[t].node());
		}
	}
	dgMod.doIt();
	dagMod.doIt();
	
}

SSeamMesh* SeamsEasyNode::getMeshPtr() {
	return &m_baseMesh;
}

MStatus SeamsEasyNode::loadProfileCurveSetting(MObject& rampAttribute, float width, float depth, int subdivisions, std::set <OffsetParams> &offsetParams) {
	MStatus status;
	std::set <OffsetParams> offsetParamsTmp;

	float span = 1 / (float)(subdivisions + 1);

	MRampAttribute rampAttr(thisMObject(), rampAttribute);

	float firstValue;
	float srcMin = -1, max = -1;

	for (int s = 0; s < subdivisions + 2; s++) {
		OffsetParams newParam;

		float param = s*span;
		newParam.index = s;
		newParam.distance = width - (param*width);

		rampAttr.getValueAtPosition(param, newParam.depth, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		if (s == 0)
			firstValue = newParam.depth;

		if (srcMin == -1 || newParam.depth < srcMin)
			srcMin = newParam.depth;

		if (max == -1 || newParam.depth > max)
			max = newParam.depth;

		offsetParamsTmp.insert(newParam);
	}

	float trgMin = depth, trgMax = 0;
	float srcMax = (firstValue == srcMin) ? max : firstValue;

	for (auto oldParam : offsetParamsTmp) {
		OffsetParams newParam = oldParam;
		newParam.depth = remap(srcMin, srcMax, trgMin, trgMax, newParam.depth);
		offsetParams.insert(newParam);
	}

	return MS::kSuccess;
}
