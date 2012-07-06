#ifndef _MESHUPMODEL_H
#define _MESHUPMODEL_H

#include <vector>
#include <list>
#include <iostream>
#include <map>
#include <boost/shared_ptr.hpp>
#include <limits>

#include "SimpleMath/SimpleMath.h"
#include "SimpleMath/SimpleMathGL.h"

#include "FrameConfig.h"
#include "MeshVBO.h"
#include "Curve.h"

typedef boost::shared_ptr<MeshVBO> MeshPtr;
typedef boost::shared_ptr<Curve> CurvePtr;

struct Frame;
typedef boost::shared_ptr<Frame> FramePtr;

/** \brief Searches in various locations for the model. */
std::string find_model_file_by_name (const std::string &model_name);

struct Frame {
	Frame() :
		name (""),
		pose_translation (0.f, 0.f, 0.f),
		pose_rotation (0.f, 0.f, 0.f),
		pose_rotation_quaternion (0.f, 0.f, 0.f, 1.f),
		pose_scaling (1.f, 1.f, 1.f),
		frame_transform (Matrix44f::Identity ()),
		parent_transform (Matrix44f::Identity ()),
		pose_transform (Matrix44f::Identity ())
	{}

	std::string name;

	Vector3f pose_translation;
	Vector3f pose_rotation;
	smQuaternion pose_rotation_quaternion;
	Vector3f pose_scaling;

	/** Transformation from base to pose */
	Matrix44f frame_transform;
	Matrix44f parent_transform;
	Matrix44f pose_transform;

	std::vector<FramePtr> children;

	/// \brief Recursively updates the pose of the Frame and its children
	void updatePoseTransform(const Matrix44f &parent_pose_transform, const FrameConfig &config);
	/** \brief Recursively updates all frames in neutral pose.
	 *
	 * As the pose information is superimposed onto the default pose we have
	 * to compute the default transformations first. This is done in this function.
	 * */
	void initDefaultFrameTransform(const Matrix44f &parent_pose_transform, const FrameConfig &config);

	Matrix33f getFrameTransformRotation() {
		return Matrix33f (
				frame_transform(0,0), frame_transform(1,0), frame_transform(2,0),
				frame_transform(0,1), frame_transform(1,1), frame_transform(2,1),
				frame_transform(0,2), frame_transform(1,2), frame_transform(2,2)
				);
	}

	Vector3f getFrameTransformTranslation() {
		return Vector3f (frame_transform(3,0), frame_transform(3,1), frame_transform (3,2));
	}
};

struct Segment {
	Segment () :
		name ("unnamed"),
		dimensions (-1.f, -1.f, -1.f),
		scale (-1.f, -1.f, -1.f),
		meshcenter (1/0.0, 0.f, 0.f),
		translate (0.f, 0.f, 0.f),
		gl_matrix (Matrix44f::Identity(4,4)),
		frame (FramePtr()),
		mesh_filename("")
	{}

	std::string name;

	Vector3f dimensions;
	Vector3f scale;
	Vector3f color;
	MeshPtr mesh;
	Vector3f meshcenter;
	Vector3f translate;
	Matrix44f gl_matrix;
	FramePtr frame;
	std::string mesh_filename;
};

struct MeshupModel {
	MeshupModel():
		model_filename (""),
		frames_initialized(false),
		skip_vbo_generation(false)
	{
		// create the BASE frame
		FramePtr base_frame (new (Frame));
		base_frame->name = "BASE";
		base_frame->parent_transform = Matrix44f::Identity();

		frames.push_back (base_frame);
		framemap["BASE"] = base_frame;
	}

	MeshupModel& operator= (const MeshupModel& other) {
		if (&other != this) {
			model_filename = other.model_filename;

			segments = other.segments;
			meshmap = other.meshmap;

			frames = other.frames;
			framemap = other.framemap;

			configuration = other.configuration;
			frames_initialized = other.frames_initialized;
		}
		return *this;
	}

	std::string model_filename;

	typedef std::list<Segment> SegmentList;
	SegmentList segments;
	typedef std::map<std::string, MeshPtr> MeshMap;
	MeshMap meshmap;
	typedef std::vector<FramePtr> FrameVector;
	FrameVector frames;
	typedef std::map<std::string, FramePtr> FrameMap;
	FrameMap framemap;
	typedef std::map<std::string, CurvePtr> CurveMap;
	CurveMap curvemap;

	/// Configuration how transformations are defined
	FrameConfig configuration;

	/// Marks whether the frame transformations have to be initialized
	bool frames_initialized;

	/// Skips vbo generation when adding segments (useful when no OpenGL
	// available)
	bool skip_vbo_generation;
	
	void addFrame (
			const std::string &parent_frame_name,
			const std::string &frame_name,
			const Matrix44f &parent_transform);

	void addSegment (
			const std::string &frame_name,
			const std::string &segment_name,
			const Vector3f &dimensions,
			const Vector3f &scale,
			const Vector3f &color,
			const std::string &mesh_name,
			const Vector3f &translate,
			const Vector3f &mesh_center);

	void addCurvePoint (
			const std::string &curve_name,
			const Vector3f &coords,
			const Vector3f &color
			);

	void updateFrames();

	FramePtr findFrame (const char* frame_name) {
		FrameMap::iterator frame_iter = framemap.find (frame_name);

		if (frame_iter == framemap.end()) {
			std::cerr << "Error: Could not find frame '" << frame_name << "'!" << std::endl;
			return FramePtr();
		}

		return frame_iter->second;
	}

	void clear() {
		segments.clear();
		frames.clear();
		framemap.clear();
		meshmap.clear();
		curvemap.clear();

		*this = MeshupModel();
	}

	/// Initializes the fixed frame transformations and sets frames_initialized to true
	void initDefaultFrameTransform();

	void draw();
	void drawFrameAxes();
	void drawBaseFrameAxes();
	void drawCurves();

	bool loadModelFromFile (const char* filename, bool strict = true);
	void saveModelToFile (const char* filename);

	bool loadModelFromJsonFile (const char* filename, bool strict = true);
	bool loadModelFromLuaFile (const char* filename, bool strict = true);
	
	void saveModelToJsonFile (const char* filename);
	void saveModelToLuaFile (const char* filename);

	bool loadAnimationFromFile (const char* filename, bool strict = true);
};

typedef boost::shared_ptr<MeshupModel> MeshupModelPtr;

#endif
