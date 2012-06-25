#include "GL/glew.h"

#include "MeshupModel.h"

#include "SimpleMath/SimpleMathGL.h"
#include "string_utils.h"

#include "json/json.h"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <ostream>
#include <stack>
#include <limits>

#include <boost/filesystem.hpp>

extern "C"
{
   #include <lua.h>
   #include <lauxlib.h>
   #include <lualib.h>
}
#include "luatables.h"

#include "objloader.h"
#include "Curve.h"

using namespace std;

const string invalid_id_characters = "{}[],;: \r\n\t";

void bail(lua_State *L, const char *msg){
	std::cerr << msg << lua_tostring(L, -1) << endl;
	abort();
}

std::string find_model_file_by_name (const std::string &model_name) {
	std::string result;

	std::vector<std::string> paths;
	paths.push_back("./");
	paths.push_back("./models/");

	if (getenv ("MESHUP_PATH")) {
		std::string env_meshup_dir (getenv("MESHUP_PATH"));

		if (env_meshup_dir.size() != 0) {
			if (env_meshup_dir[env_meshup_dir.size() - 1] != '/')
				env_meshup_dir += '/';

			paths.push_back (env_meshup_dir);
			paths.push_back (env_meshup_dir + "models/");
		}
	}

	paths.push_back("/usr/local/share/meshup/models/");
	paths.push_back("/usr/share/meshup/models/");

	std::vector<std::string>::iterator iter = paths.begin();
	string model_filename;
	for (iter; iter != paths.end(); iter++) {
		model_filename = *iter + model_name;

//		cout << "checking " << model_filename << endl;
		if (boost::filesystem::is_regular_file(model_filename))
			break;

		model_filename += ".json";
//		cout << "checking " << model_filename << endl;

		if (boost::filesystem::is_regular_file(model_filename))
			break;
	}

	if (iter != paths.end())
		return model_filename;

	return std::string("");
}

std::string find_mesh_file_by_name (const std::string &filename) {
	std::string result;

	std::vector<std::string> paths;
	paths.push_back("./");

	if (getenv ("MESHUP_PATH")) {
		std::string env_meshup_dir (getenv("MESHUP_PATH"));

		if (env_meshup_dir.size() != 0) {
			if (env_meshup_dir[env_meshup_dir.size() - 1] != '/')
				env_meshup_dir += '/';

			paths.push_back (env_meshup_dir);
		}
	}

	paths.push_back("/usr/local/share/meshup/meshes/");
	paths.push_back("/usr/share/meshup/meshes/");

	std::vector<std::string>::iterator iter = paths.begin();
	for (iter; iter != paths.end(); iter++) {
		std::string test_path = *iter;

		if (!boost::filesystem::is_regular_file(test_path + filename))
			continue;

		break;
	}

	if (iter != paths.end())
		return (string(*iter) + string(filename));

	cerr << "Could not find mesh file " << filename << ". Search path: " << endl;
	for (iter = paths.begin(); iter != paths.end(); iter++) {
		cout << "  " << *iter << endl;
	}
	exit(1);

	return std::string("");
}

std::string sanitize_frame_name (const std::string &frame_name) {
	string frame_name_sanitized = frame_name;
	if (is_numeric(frame_name)) {
		cerr << "Warning invalid frame name '" << frame_name << "': frame name should not be numeric only!" << endl;
		frame_name_sanitized = string("_") + frame_name;
	}

	// check for invalid characters
	if (frame_name.find_first_of (invalid_id_characters) != string::npos) {
		cerr << "Error: Found invalid character '"
			<< frame_name[frame_name.find_first_of (invalid_id_characters)]
			<< "' in frame name '" << frame_name << "'!" << endl;
		exit (1);
	}

	return frame_name_sanitized;
}

/*
 * Frame
 */
void Frame::updatePoseTransform(const Matrix44f &parent_pose_transform, const FrameConfig &config) {
	// first translate, then rotate as specified in the angles
	pose_transform = 
		frame_transform
		* parent_pose_transform;

	// apply pose transform
	pose_transform =
		smScale (pose_scaling[0], pose_scaling[1], pose_scaling[2])
	  * pose_rotation_quaternion.toGLMatrix()
		* smTranslate (pose_translation[0], pose_translation[1], pose_translation[2])
		* pose_transform;

	for (unsigned int ci = 0; ci < children.size(); ci++) {
		children[ci]->updatePoseTransform (pose_transform, config);
	}
}

/**
 *
 * \todo get rid of initDefaultFrametransform
 */
void Frame::initDefaultFrameTransform(const Matrix44f &parent_frame_transform, const FrameConfig &config) {
	// first translate, then rotate as specified in the angles
	frame_transform =	parent_transform;

	for (unsigned int ci = 0; ci < children.size(); ci++) {
		children[ci]->initDefaultFrameTransform (frame_transform, config);
	}
}

/*********************************
 * FrameAnimationTrack
 *********************************/
FramePose FrameAnimationTrack::interpolatePose (float time) {
	if (poses.size() == 0) {
		return FramePose();
	} else if (poses.size() == 1) {
		return *poses.begin();
	}

	// at this point we have at least two poses
	FramePoseList::iterator pose_iter = poses.begin();

	FramePose start_pose (*pose_iter);
	pose_iter++;
	FramePose end_pose = (*pose_iter);

	// find the two frames that surround the time
	while (pose_iter != poses.end() && end_pose.timestamp <= time) {
		start_pose = end_pose;
		pose_iter++;
		end_pose = *pose_iter;
	}

	// if we overshot we have to return the last valid frame (i.e.
	// start_pose) 
	if (pose_iter == poses.end())
		end_pose = start_pose;

//	cout << "start time = " << start_pose.timestamp << " end time = " << end_pose.timestamp << " query time = " << time << endl;

	// we use end_pose as the result
	float duration = end_pose.timestamp - start_pose.timestamp;
	if (end_pose.timestamp - start_pose.timestamp == 0.f)
		return start_pose;

	float fraction = (time - start_pose.timestamp) / (end_pose.timestamp - start_pose.timestamp);
	
	// some handling for over- and undershooting
	if (fraction > 1.f)
		fraction = 1.f;
	if (fraction < 0.f)
		fraction = 0.f;

	// perform the interpolation
	end_pose.timestamp = start_pose.timestamp + fraction * (end_pose.timestamp - start_pose.timestamp);
	end_pose.translation = start_pose.translation + fraction * (end_pose.translation - start_pose.translation);
	end_pose.rotation = start_pose.rotation + fraction * (end_pose.rotation - start_pose.rotation);
	
	end_pose.rotation_quaternion = start_pose.rotation_quaternion.slerp (fraction, end_pose.rotation_quaternion);
	end_pose.scaling = start_pose.scaling + fraction * (end_pose.scaling - start_pose.scaling);

	return end_pose;
}

/*********************************
 * MeshupModel
 *********************************/
void MeshupModel::addFrame (
		const std::string &parent_frame_name,
		const std::string &frame_name,
		const Matrix44f &parent_transform) {
	// cout << "addFrame(" << endl
	//	<< "  parent_frame_name = " << parent_frame_name << endl
	//	<< "  frame_name = " << frame_name << endl
	//	<< "  parent_transform = " << endl << parent_transform << endl;

	// mark frame transformations as dirty
	frames_initialized = false;

	string frame_name_sanitized = sanitize_frame_name (frame_name);
	string parent_frame_name_sanitized = sanitize_frame_name (parent_frame_name);

	// create the frame
	FramePtr frame (new Frame);
	frame->name = frame_name_sanitized;
	frame->parent_transform = parent_transform;
	frame->frame_transform = parent_transform;

	// first find the frame
	FramePtr parent_frame = findFrame (parent_frame_name_sanitized.c_str());
	if (parent_frame == NULL) {
		cerr << "Could not find frame '" << parent_frame_name_sanitized << "'!" << endl;
		exit (1);
	}

	parent_frame->children.push_back (frame);
	framemap[frame->name] = frame;
}

void MeshupModel::addSegment (
		const std::string &frame_name,
		const std::string &segment_name,
		const Vector3f &dimensions,
		const Vector3f &scale,
		const Vector3f &color,
		const std::string &mesh_name,
		const Vector3f &translate,
		const Vector3f &mesh_center) {
	Segment segment;
	segment.name = segment_name;
	segment.dimensions = configuration.axes_rotation.transpose() * dimensions;

	//~ // make sure that the dimensions are all positive
	//~ for (int i = 0; i < 3; i++) {
	//~ if (segment.dimensions[i] < 0)
		//~ segment.dimensions[i] = -segment.dimensions[i];
	//~ }

	segment.color = color;
	segment.scale = scale;
	segment.translate = translate;

	// check whether we have the mesh, if not try to load it
	MeshMap::iterator mesh_iter = meshmap.find (mesh_name);
	if (mesh_iter == meshmap.end()) {
		MeshPtr new_mesh (new MeshVBO);

		// check whether we want to extract a sub object within the obj file
		string mesh_filename = mesh_name;
		string submesh_name = "";
		if (mesh_name.find (':') != string::npos) {
			submesh_name = mesh_name.substr (mesh_name.find(':') + 1, mesh_name.size());
			mesh_filename = mesh_name.substr (0, mesh_name.find(':'));
			string mesh_file_location = find_mesh_file_by_name (mesh_filename);
			cout << "Loading sub object " << submesh_name << " from file " << mesh_file_location << endl;
			load_obj (*new_mesh, mesh_file_location.c_str(), submesh_name.c_str());
		} else {
			string mesh_file_location = find_mesh_file_by_name (mesh_name);
			cout << "Loading mesh " << mesh_file_location << endl;
			load_obj (*new_mesh, mesh_file_location.c_str());
		}

		if (!skip_vbo_generation)
			new_mesh->generate_vbo();

		meshmap[mesh_name] = new_mesh;

		mesh_iter = meshmap.find (mesh_name);
	}

	segment.mesh = mesh_iter->second;
	segment.meshcenter = configuration.axes_rotation.transpose() * mesh_center;
	segment.frame = findFrame (sanitize_frame_name(frame_name).c_str());
	segment.mesh_filename = mesh_name;
	assert (segment.frame != NULL);
	segments.push_back (segment);
}

void MeshupModel::addFramePose (
		const std::string &frame_name,
		float time,
		const Vector3f &frame_translation,
		const Vector3f &frame_rotation,
		const Vector3f &frame_scaling
		) {
	FramePtr frame = findFrame (sanitize_frame_name(frame_name).c_str());
	FramePose pose;
	pose.timestamp = time;
	pose.translation = configuration.axes_rotation.transpose() * frame_translation;
	pose.rotation = frame_rotation;
	pose.rotation_quaternion = configuration.convertAnglesToQuaternion (frame_rotation);
	pose.scaling = frame_scaling;

	animation.frametracks[frame].poses.push_back(pose);

	// update the duration of the animation
	if (time > animation.duration)
		animation.duration = time;
}

void MeshupModel::addCurvePoint (
		const std::string &curve_name,
		const Vector3f &coords,
		const Vector3f &color
		) {
	CurveMap::iterator curve_iter = curvemap.find(curve_name);
	if (curve_iter == curvemap.end()) {
		curvemap[curve_name] = CurvePtr (new Curve);
	}

	CurvePtr curve = curvemap[curve_name];
	curve->addPointWithColor (
			coords[0], coords[1], coords[2],
			color[0], color[1], color[2]
			);
}

void MeshupModel::initDefaultFrameTransform() {
	Matrix44f base_transform (Matrix44f::Identity());

	for (unsigned int bi = 0; bi < frames.size(); bi++) {
		frames[bi]->initDefaultFrameTransform (base_transform, configuration);
	}

	frames_initialized = true;
}

void MeshupModel::updatePose() {
	// if there is no animation we can return
	if (animation.frametracks.size() != 0) {
		if (animation.current_time > animation.duration) {
			if (animation.loop) {
				while (animation.current_time > animation.duration)
					animation.current_time -= animation.duration;
			} else {
				animation.current_time = animation.duration;
			}
		}
	}
	FrameAnimationTrackMap::iterator frame_track_iter = animation.frametracks.begin();

	while (frame_track_iter != animation.frametracks.end()) {
		FramePose pose = frame_track_iter->second.interpolatePose (animation.current_time);
		frame_track_iter->first->pose_translation = pose.translation;
		frame_track_iter->first->pose_rotation = pose.rotation;
		frame_track_iter->first->pose_rotation_quaternion = pose.rotation_quaternion;
		frame_track_iter->first->pose_scaling = pose.scaling;

		frame_track_iter++;
	}
}

void MeshupModel::updateFrames() {
	Matrix44f base_transform (Matrix44f::Identity());

	// check whether the frame transformations are valid
	if (frames_initialized == false)
		initDefaultFrameTransform();

	for (unsigned int bi = 0; bi < frames.size(); bi++) {
		frames[bi]->updatePoseTransform (base_transform, configuration);
	}
}

void MeshupModel::updateSegments() {
	SegmentList::iterator seg_iter = segments.begin();

	while (seg_iter != segments.end()) {
		Vector3f bbox_size (seg_iter->mesh->bbox_max - seg_iter->mesh->bbox_min);

		Vector3f scale(1.0f,1.0f,1.0f) ;

		//only scale, if the dimensions are valid, i.e. are set in json-File
		if (seg_iter->dimensions[0] != 0.f) {
			scale = Vector3f(
					fabs(seg_iter->dimensions[0]) / bbox_size[0],
					fabs(seg_iter->dimensions[1]) / bbox_size[1],
					fabs(seg_iter->dimensions[2]) / bbox_size[2]
					);
		} else if (seg_iter->scale[0] > 0.f) {
			scale=seg_iter->scale;
		}
		
		Vector3f translate(0.0f,0.0f,0.0f);
		//only translate with meshcenter if it is defined in json file
		if (!isnan(seg_iter->meshcenter[0])) {
				Vector3f center ( seg_iter->mesh->bbox_min + bbox_size * 0.5f);
				translate[0] = -center[0] * scale[0] + seg_iter->meshcenter[0];
				translate[1] = -center[1] * scale[1] + seg_iter->meshcenter[1];
				translate[2] = -center[2] * scale[2] + seg_iter->meshcenter[2];
		}
		translate+=seg_iter->translate;
		
		// we also have to apply the scaling after the transform:
		seg_iter->gl_matrix = 
			smScale (scale[0], scale[1], scale[2])
			* smTranslate (translate[0], translate[1], translate[2])
			* seg_iter->frame->pose_transform;

		seg_iter++;
	}
}

void MeshupModel::draw() {
	// save current state of GL_NORMALIZE to properly restore the original
	// state
	bool normalize_enabled = glIsEnabled (GL_NORMALIZE);
	if (!normalize_enabled)
		glEnable (GL_NORMALIZE);

	updateSegments();

	SegmentList::iterator seg_iter = segments.begin();

	while (seg_iter != segments.end()) {
		glPushMatrix();
		
		glMultMatrixf (seg_iter->gl_matrix.data());

		// drawing
		glColor3f (seg_iter->color[0], seg_iter->color[1], seg_iter->color[2]);
		
		seg_iter->mesh->draw(GL_TRIANGLES);

		glPopMatrix();

		seg_iter++;
	}

	// disable normalize if it was previously not enabled
	if (!normalize_enabled)
		glDisable (GL_NORMALIZE);
}

void MeshupModel::drawFrameAxes() {
	// backup the depth test and line width values
	bool depth_test_enabled = glIsEnabled (GL_DEPTH_TEST);
	if (depth_test_enabled)
		glDisable (GL_DEPTH_TEST);

	bool light_enabled = glIsEnabled (GL_LIGHTING);
	if (light_enabled)
		glDisable (GL_LIGHTING);

	float line_width;
	glGetFloatv (GL_LINE_WIDTH, &line_width);

	glLineWidth (2.f);

	// for the rotation of the axes
	Matrix44f axes_rotation_matrix (Matrix44f::Identity());
	axes_rotation_matrix.block<3,3> (0,0) = configuration.axes_rotation;

	FrameMap::iterator frame_iter = framemap.begin();

	while (frame_iter != framemap.end()) {
		if (frame_iter->second->name == "BASE") {
			frame_iter++;
			continue;
		}
		glPushMatrix();

			
		Matrix44f transform_matrix = axes_rotation_matrix * frame_iter->second->pose_transform;
		glMultMatrixf (transform_matrix.data());

		glBegin (GL_LINES);
		glColor3f (1.f, 0.f, 0.f);
		glVertex3f (0.f, 0.f, 0.f);
		glVertex3f (0.1f, 0.f, 0.f);
		glColor3f (0.f, 1.f, 0.f);
		glVertex3f (0.f, 0.f, 0.f);
		glVertex3f (0.f, 0.1f, 0.f);
		glColor3f (0.f, 0.f, 1.f);
		glVertex3f (0.f, 0.f, 0.f);
		glVertex3f (0.f, 0.f, 0.1f);
		glEnd();

		glPopMatrix();

		frame_iter++;
	}

	if (depth_test_enabled)
		glEnable (GL_DEPTH_TEST);

	if (light_enabled)
		glEnable (GL_LIGHTING);

	glLineWidth (line_width);
}

void MeshupModel::drawBaseFrameAxes() {
	// backup the depth test and line width values
	bool depth_test_enabled = glIsEnabled (GL_DEPTH_TEST);
	if (depth_test_enabled)
		glDisable (GL_DEPTH_TEST);

	bool light_enabled = glIsEnabled (GL_LIGHTING);
	if (light_enabled)
		glDisable (GL_LIGHTING);

	float line_width;
	glGetFloatv (GL_LINE_WIDTH, &line_width);

	glLineWidth (2.f);

	// for the rotation of the axes
	Matrix44f axes_rotation_matrix (Matrix44f::Identity());
	axes_rotation_matrix.block<3,3> (0,0) = configuration.axes_rotation;

	glPushMatrix();

	Matrix44f transform_matrix = axes_rotation_matrix * framemap["BASE"]->pose_transform;
	glMultMatrixf (transform_matrix.data());

	glBegin (GL_LINES);
	glColor3f (1.f, 0.f, 0.f);
	glVertex3f (0.f, 0.f, 0.f);
	glVertex3f (1.f, 0.f, 0.f);
	glColor3f (0.f, 1.f, 0.f);
	glVertex3f (0.f, 0.f, 0.f);
	glVertex3f (0.f, 1.f, 0.f);
	glColor3f (0.f, 0.f, 1.f);
	glVertex3f (0.f, 0.f, 0.f);
	glVertex3f (0.f, 0.f, 1.f);
	glEnd();

	glPopMatrix();

	if (depth_test_enabled)
		glEnable (GL_DEPTH_TEST);

	if (light_enabled)
		glEnable (GL_LIGHTING);

	glLineWidth (line_width);
}

void MeshupModel::drawCurves() {
	CurveMap::iterator curve_iter = curvemap.begin();
	while (curve_iter != curvemap.end()) {
		curve_iter->second->draw();
		curve_iter++;
	}
}

Json::Value vec3_to_json (const Vector3f &vec) {
	Json::Value result;
	result[0] = Json::Value(static_cast<float>(vec[0]));
	result[1] = Json::Value(static_cast<float>(vec[1]));
	result[2] = Json::Value(static_cast<float>(vec[2]));

	return result;
}

Vector3f json_to_vec3 (const Json::Value &value, Vector3f default_value=Vector3f (0.f, 0.f, 0.f)) {
	if (value.isNull())
		return default_value;

	Vector3f result (
			value[0].asFloat(),
			value[1].asFloat(),
			value[2].asFloat()
			);

	return result;
}

Json::Value frame_configuration_to_json_value (const FrameConfig &config) {
	using namespace Json;

	Value result;
	result["axis_front"] = vec3_to_json (config.axis_front);
	result["axis_up"] = vec3_to_json (config.axis_up);
	result["axis_right"] = vec3_to_json (config.axis_right);

	result["rotation_order"][0] = config.rotation_order[0];
	result["rotation_order"][1] = config.rotation_order[1];
	result["rotation_order"][2] = config.rotation_order[2];

	return result;
}

Json::Value frame_to_json_value (const FramePtr &frame, FrameConfig frame_config) {
	using namespace Json;

	Value result;

	result["name"] = frame->name;

	result["parent_translation"] = vec3_to_json(frame_config.axes_rotation * frame->getFrameTransformTranslation());
	
	Matrix33f rotation = frame->getFrameTransformRotation();
	if (Matrix33f::Identity() != rotation) {
		cerr << "Error: cannot convert non-zero parent_rotation to Json value." << endl;
		abort();
	}

	return result;
}

Json::Value segment_to_json_value (const Segment &segment, FrameConfig frame_config) {
	using namespace Json;

	Value result;

	result["name"] = segment.name;

	if (Vector3f::Zero() != segment.dimensions)
		result["dimensions"] = vec3_to_json (frame_config.axes_rotation * segment.dimensions);

	if (Vector3f::Zero() != segment.color)
		result["color"] = vec3_to_json (segment.color);

	if (Vector3f::Zero() != segment.scale)
		result["scale"] = vec3_to_json (segment.scale);

	if (!isnan(segment.meshcenter[0])) {
		result["mesh_center"] = vec3_to_json (frame_config.axes_rotation * segment.meshcenter);
	} else {
		result["translate"] = vec3_to_json (frame_config.axes_rotation * segment.translate);
	}

	result["mesh_filename"] = segment.mesh_filename;
	result["frame"] = segment.frame->name;

	return result;
}

void MeshupModel::saveModelToJsonFile (const char* filename) {
	// we absoulutely have to set the locale to english for numbers.
	// Otherwise we might wrongly formatted data. 
	std::setlocale(LC_NUMERIC, "POSIX");
	Json::Value root_node;

	root_node["configuration"] = frame_configuration_to_json_value (configuration);

	int frame_index = 0;
	// we have to write out the frames recursively
	for (int bi = 0; bi < frames.size(); bi++) {
		stack<FramePtr> frame_stack;
		frame_stack.push (frames[bi]);

		stack<int> child_index_stack;
		if (frame_stack.top()->children.size() > 0) {
			child_index_stack.push(0);
		}

		if (frame_stack.top()->name != "BASE") {
			root_node["frames"][frame_index] = frame_to_json_value(frame_stack.top(), configuration);
			frame_index++;
		}

		while (frame_stack.size() > 0) {
			FramePtr cur_frame = frame_stack.top();
			int child_idx = child_index_stack.top();

			if (child_idx < cur_frame->children.size()) {
				FramePtr child_frame = cur_frame->children[child_idx];

				root_node["frames"][frame_index] = frame_to_json_value(child_frame, configuration);
				root_node["frames"][frame_index]["parent"] = cur_frame->name;
				frame_index++;
				
				child_index_stack.pop();
				child_index_stack.push (child_idx + 1);

				if (child_frame->children.size() > 0) {
					frame_stack.push (child_frame);
					child_index_stack.push(0);
				}
			} else {
				frame_stack.pop();
				child_index_stack.pop();
			}

//			frame_stack.pop();
		}
	}

	// segments
	
	int segment_index = 0;
	SegmentList::iterator seg_iter = segments.begin();
	while (seg_iter != segments.end()) {
		root_node["segments"][segment_index] = segment_to_json_value (*seg_iter, configuration);

		segment_index++;
		seg_iter++;
	}

	ofstream file_out (filename, ios::trunc);
	file_out << root_node << endl;
	file_out.close();
}

string vec3_to_string_no_brackets (const Vector3f &vector) {
	ostringstream out;
	out << vector[0] << ", " << vector[1] << ", " << vector[2];

	return out.str();
}

string frame_to_lua_string (const FramePtr frame, const string &parent_name, vector<string> meshes, int indent = 0) {
	ostringstream out;
	string indent_str;

	for (int i = 0; i < indent; i++)
		indent_str += "  ";

	out << indent_str << "{" << endl
		<< indent_str << "  name = \"" << frame->name << "\"," << endl
		<< indent_str << "  parent = \"" << parent_name << "\"," << endl;

	Vector3f translation = frame->getFrameTransformTranslation();
	Matrix33f rotation = frame->getFrameTransformRotation();

	// only write joint_transform if we actually have a transformation
	if (Vector3f::Zero() != translation
			|| Matrix33f::Identity() != rotation) {
		out << indent_str << "  joint_transform = {" << endl;

		if (Vector3f::Zero() != translation)
			out << indent_str << "    r = { " << vec3_to_string_no_brackets (translation) << " }," << endl;

		if (Matrix33f::Identity() != rotation) {
			out << indent_str << "    E = {" << endl;
			for (unsigned int i = 0; i < 3; i++) {
			out << indent_str << "      { ";
				for (unsigned int j = 0; j < 2; j++) {
					out << setiosflags(ios_base::fixed) << rotation(i,j) << ", ";
				}
				out << setiosflags(ios_base::fixed) << rotation(i,2) << " }," << endl;
			}
			out << indent_str << "    }," << endl;
		}
		out << indent_str << "  }," << endl;
	}

	// output of the meshes
	if (meshes.size() > 0) {
		out << indent_str << "  visuals = {" << endl;

		for (unsigned int i = 0; i < meshes.size(); i++) {
			out << indent_str << "    " << meshes[i] << "," << endl;
		}

		out << indent_str << "  }," << endl;
	}

	out << indent_str << "}";

	return out.str();
}

string segment_to_lua_string (const Segment &segment, FrameConfig frame_config, int indent = 0) {
	ostringstream out;
	string indent_str;

	for (int i = 0; i < indent; i++)
		indent_str += "  ";

	out << indent_str << segment.name << " = {" << endl
		<< indent_str << "  name = \"" << segment.name << "\"," << endl;
	if (Vector3f::Zero() != segment.dimensions)
		out << indent_str << "  dimensions = { " 
			<< vec3_to_string_no_brackets(frame_config.axes_rotation * segment.dimensions) 
			<< "}," << endl;

	if (Vector3f(1.f, 1.f, 1.f) != segment.scale)
		out	<< indent_str << "  scale = { " << vec3_to_string_no_brackets(segment.scale) << "}," << endl;

	if (Vector3f::Zero() != segment.color)
		out	<< indent_str << "  color = { " << vec3_to_string_no_brackets(segment.color) << "}," << endl;

	if (Vector3f::Zero() != segment.meshcenter)
		out	<< indent_str << "  mesh_center = { " 
			<< vec3_to_string_no_brackets(frame_config.axes_rotation * segment.meshcenter)
			<< "}," << endl;

	if (Vector3f::Zero() != segment.translate)
		out	<< indent_str << "  translate = { " << vec3_to_string_no_brackets(segment.translate) << "}," << endl;

	out	<< indent_str << "  src = \"" << segment.mesh_filename << "\"," << endl;
	out << indent_str << "}," << endl;

	return out.str();
}

void MeshupModel::saveModelToLuaFile (const char* filename) {
	cout << __func__ << endl;
	ofstream file_out (filename, ios::trunc);

	map<string, vector<string> > frame_segment_map;

	// write all segments
	file_out << "meshes = {" << endl;
	SegmentList::iterator seg_iter = segments.begin();
	while (seg_iter != segments.end()) {
		file_out << segment_to_lua_string (*seg_iter, configuration, 1);

		frame_segment_map[seg_iter->frame->name].push_back(string("meshes.") + seg_iter->name);

		seg_iter++;
	}
	file_out << "}" << endl << endl;

	// write configuration
	file_out << "model = {" << endl
		<< "  configuration = {" << endl
		<< "    axis_front = { " << vec3_to_string_no_brackets(configuration.axis_front) << " }," << endl
		<< "    axis_up    = { " << vec3_to_string_no_brackets(configuration.axis_up) << " }," << endl
		<< "    axis_right = { " << vec3_to_string_no_brackets(configuration.axis_right) << " }," << endl
		<< "    rotation_order = { " << configuration.rotation_order[0] << ", "
			<< configuration.rotation_order[1] << ", "
			<< configuration.rotation_order[2] << "}," << endl
		<< "  }," << endl << endl;

	// write frames
	file_out << "  frames = {" << endl;
	int frame_index = 0;
	// we have to write out the frames recursively
	for (int bi = 0; bi < frames.size(); bi++) {
		stack<FramePtr> frame_stack;
		frame_stack.push (frames[bi]);

		stack<int> child_index_stack;
		if (frame_stack.top()->children.size() > 0) {
			child_index_stack.push(0);
		}

		if (frame_stack.top()->name != "BASE") {
			file_out << frame_to_lua_string(frame_stack.top(), "BASE", frame_segment_map["BASE"], 2) << "," << endl;
			frame_index++;
		}

		while (frame_stack.size() > 0) {
			FramePtr cur_frame = frame_stack.top();
			int child_idx = child_index_stack.top();

			if (child_idx < cur_frame->children.size()) {
				FramePtr child_frame = cur_frame->children[child_idx];

				file_out << frame_to_lua_string(child_frame, cur_frame->name, frame_segment_map[child_frame->name], 2) << "," << endl;
				frame_index++;
				
				child_index_stack.pop();
				child_index_stack.push (child_idx + 1);

				if (child_frame->children.size() > 0) {
					frame_stack.push (child_frame);
					child_index_stack.push(0);
				}
			} else {
				frame_stack.pop();
				child_index_stack.pop();
			}
		}
	}
	file_out << "  }" << endl;
	file_out << "}" << endl << endl;
	file_out << "return model" << endl;

	file_out.close();
}

bool MeshupModel::loadModelFromFile (const char* filename, bool strict) {
	string filename_str (filename);

	if (filename_str.size() < 5) {
		cerr << "Error: Filename " << filename << " too short. Must be at least 5 characters." << endl;

		if (strict)
			abort();

		return false;
	}

	if (tolower(filename_str.substr(filename_str.size() - 4, 4)) == ".lua")
		return loadModelFromLuaFile (filename, strict);
	else if (tolower(filename_str.substr(filename_str.size() - 5, 5)) == ".json")
		return loadModelFromJsonFile (filename, strict);

	cerr << "Error: Could not determine filetype for model " << filename << ". Must be either .lua or .json file." << endl;

	if (strict)
		abort();

	return false;
}

void MeshupModel::saveModelToFile (const char* filename) {
	string filename_str (filename);

	if (filename_str.size() < 5) {
		cerr << "Error: Filename " << filename << " too short. Must be at least 5 characters." << endl;
		abort();
	}

	if (tolower(filename_str.substr(filename_str.size() - 4, 4)) == ".lua")
		saveModelToLuaFile (filename);
	else if (tolower(filename_str.substr(filename_str.size() - 5, 5)) == ".json")
		saveModelToJsonFile (filename);

	else {
		cerr << "Error: Could not determine filetype for model " << filename << ". Must be either .lua or .json file." << endl;
		abort();
	}
}

bool MeshupModel::loadModelFromJsonFile (const char* filename, bool strict) {
	// we absoulutely have to set the locale to english for numbers.
	// Otherwise we might read false values due to the wrong conversion.
	std::setlocale(LC_NUMERIC, "POSIX");

	using namespace Json;
	Value root;
	Reader reader;

	ifstream file_in (filename);

	if (!file_in) {
		cerr << "Error opening file " << filename << "!" << endl;
		
		if (strict)
			abort();

		return false;
	}

	cout << "Loading model " << filename << endl;

	stringstream buffer;
	buffer << file_in.rdbuf();
	file_in.close();

	bool parsing_result = reader.parse (buffer.str(), root);
	if (!parsing_result) {
		cerr << "Error reading model: " << reader.getFormattedErrorMessages();

		if (strict)
			abort ();

		return false;
	}

	// clear the model
	clear();

	// read the configuration, fill with default values if they do not exist
	if (root["configuration"]["axis_front"].isNull())
		root["configuration"]["axis_front"] = vec3_to_json (Vector3f (1.f, 0.f, 0.f));
	if (root["configuration"]["axis_up"].isNull())
		root["configuration"]["axis_up"] = vec3_to_json (Vector3f (0.f, 1.f, 0.f));
	if (root["configuration"]["axis_right"].isNull())
		root["configuration"]["axis_right"] = vec3_to_json (Vector3f (0.f, 0.f, 1.f));
	if (root["configuration"]["rotation_order"][0].isNull())
		root["configuration"]["rotation_order"][0] = 2;
	if (root["configuration"]["rotation_order"][1].isNull())
		root["configuration"]["rotation_order"][1] = 1;
	if (root["configuration"]["rotation_order"][2].isNull())
		root["configuration"]["rotation_order"][2] = 0;


	configuration.axis_front = json_to_vec3(root["configuration"]["axis_front"]);
	configuration.axis_up = json_to_vec3(root["configuration"]["axis_up"]);
	configuration.axis_right = json_to_vec3(root["configuration"]["axis_right"]);
	configuration.rotation_order[0] = root["configuration"]["rotation_order"][0].asInt();
	configuration.rotation_order[1] = root["configuration"]["rotation_order"][1].asInt();
	configuration.rotation_order[2] = root["configuration"]["rotation_order"][2].asInt();

	configuration.init();

//	cout << "front: " << configuration.axis_front.transpose() << endl;
//	cout << "up   : " << configuration.axis_up.transpose() << endl;
//	cout << "right: " << configuration.axis_right.transpose() << endl;
//
//	cout << "rot  : " << configuration.rotation_order[0] 
//		<< ", " << configuration.rotation_order[1] 
//		<< ", " << configuration.rotation_order[2] << endl;
//
//	cout << "axes: " << endl << configuration.axes_rotation << endl;	

	// read the frames:
	ValueIterator node_iter = root["frames"].begin();

	while (node_iter != root["frames"].end()) {
		Value frame_node = *node_iter;

		Vector3f parent_translation = configuration.axes_rotation.transpose() * json_to_vec3(frame_node["parent_translation"]);
		Vector3f parent_rotation = json_to_vec3(frame_node["parent_rotation"]);

		Matrix44f parent_transform = configuration.convertAnglesToMatrix (parent_rotation) 
			* smTranslate (parent_translation[0], parent_translation[1], parent_translation[2]);

		addFrame (
				frame_node["parent"].asString(),
				frame_node["name"].asString(),
				parent_transform);

		node_iter++;
	}

	node_iter = root["segments"].begin();
	while (node_iter != root["segments"].end()) {
		Value segment_node = *node_iter;

		addSegment (
				segment_node["frame"].asString(),
				segment_node["name"].asString(),
				json_to_vec3 (segment_node["dimensions"]),
				json_to_vec3 (segment_node["scale"]),
				json_to_vec3 (segment_node["color"]),
				segment_node["mesh_filename"].asString(),
				json_to_vec3 (segment_node["translate"]),
				json_to_vec3 (segment_node["mesh_center"], Vector3f(1/0.0,1/0.0,1/0.0))
				);

		node_iter++;
	}

	initDefaultFrameTransform();

	model_filename = filename;

	return true;
}

Vector3f lua_get_vector3f (lua_State *L, const string &path, int index = -1) {
	Vector3f result;

	std::vector<double> array = get_array (L, path, index);
	if (array.size() != 3) {
		cerr << "Invalid array size for 3d vector variable '" << path << "'." << endl;
		abort();
	}

	for (unsigned int i = 0; i < 3; i++) {
		result[i] = static_cast<float>(array[i]);
	}

	return result;
}

Matrix33f lua_get_matrix3f (lua_State *L, const string &path) {
	Matrix33f result;

	// two ways either as flat array or as a lua table with three columns
	if (get_length (L, path, -1) == 3) {
		Vector3f row = lua_get_vector3f (L, path, 1);
		result(0,0) = row[0];
		result(0,1) = row[1];
		result(0,2) = row[2];

		row = lua_get_vector3f (L, path, 2);
		result(1,0) = row[0];
		result(1,1) = row[1];
		result(1,2) = row[2];

		row = lua_get_vector3f (L, path, 3);
		result(1,0) = row[0];
		result(1,1) = row[1];
		result(1,2) = row[2];

		return result;
	}

	std::vector<double> array = get_array (L, path, -1);
	if (array.size() != 9) {
		cerr << "Invalid array size for 3d matrix variable '" << path << "'." << endl;
		abort();
	}

	for (unsigned int i = 0; i < 9; i++) {
		result.data()[i] = static_cast<float>(array[i]);
	}

	return result;
}

bool lua_read_frame (
		lua_State *L,
		const string &frame_path,
		string &frame_name,
		string &parent_name,
		Vector3f &parent_translation,
		Matrix33f &parent_rotation ) {
	string path;

	if (!value_exists (L, frame_path + ".name")) {
		cerr << "Error: required value .name does not exist for frame '" << frame_path << "'!" << endl;
		return false;
	}
	frame_name = get_string (L, frame_path + ".name");

	if (!value_exists (L, frame_path + ".parent")) {
		cerr << "Error: required value .parent does not exist for frame '" << frame_name << "'!" << endl;
		return false;
	}
	parent_name = get_string (L, frame_path + ".parent");

	parent_translation = Vector3f::Zero();
	parent_rotation = Matrix33f::Identity();
	if (value_exists (L, frame_path + ".joint_transform")) {
		if (value_exists (L, frame_path + ".joint_transform.r")) {
			parent_translation = lua_get_vector3f (L, frame_path + ".joint_transform.r");
		}

		if (value_exists (L, frame_path + ".joint_transform.E")) {
			parent_rotation = lua_get_matrix3f (L, frame_path + ".joint_transform.E");
		}
	}

	return true;
}

bool lua_read_visual_info (
		lua_State *L,
		const string &visual_path,	
		std::string &segment_name,
		Vector3f &dimensions,
		Vector3f &scale,
		Vector3f &color,
		std::string &mesh_filename,
		Vector3f &translate,
		Vector3f &mesh_center) {

	if (value_exists (L, visual_path + ".name")) 
		segment_name = get_string (L, visual_path + ".name");

	if (value_exists (L, visual_path + ".dimensions"))
		dimensions = lua_get_vector3f (L, visual_path + ".dimensions");

	if (value_exists (L, visual_path + ".scale"))
		scale = lua_get_vector3f (L, visual_path + ".scale");

	if (value_exists (L, visual_path + ".color"))
		color = lua_get_vector3f (L, visual_path + ".color");

	if (value_exists (L, visual_path + ".translate"))
		translate = lua_get_vector3f (L, visual_path + ".translate");

	if (value_exists (L, visual_path + ".mesh_center"))
		mesh_center = lua_get_vector3f (L, visual_path + ".mesh_center");

	if (value_exists (L, visual_path + ".src"))
		mesh_filename = get_string (L, visual_path + ".src");

	return true;
}

bool MeshupModel::loadModelFromLuaFile (const char* filename, bool strict) {
	lua_State *L;
	L = luaL_newstate();
	luaL_openlibs(L);

	if (luaL_loadfile(L, filename) || lua_pcall (L, 0, 1, 0)) {
		cerr <<  "Error running file: ";
		std::cerr << lua_tostring(L, -1) << endl;
		if (strict)
			abort();

		return false;
	}

	clear();
	
	// configuration
	if (value_exists (L, "configuration.axis_front")) {
		configuration.axis_front = lua_get_vector3f (L, "configuration.axis_front");	
	}
	if (value_exists (L, "configuration.axis_up")) {
		configuration.axis_up = lua_get_vector3f (L, "configuration.axis_up");	
	}
	if (value_exists (L, "configuration.axis_right")) {
		configuration.axis_right = lua_get_vector3f (L, "configuration.axis_right");	
	}
	if (value_exists (L, "configuration.rotation_order")) {
		Vector3f rotation_order = lua_get_vector3f (L, "configuration.rotation_order");
		configuration.rotation_order[0] = static_cast<int>(rotation_order[0]);
		configuration.rotation_order[1] = static_cast<int>(rotation_order[1]);
		configuration.rotation_order[2] = static_cast<int>(rotation_order[2]);
	}

	configuration.init();

	// frames
	vector<string> frame_keys = get_keys (L, "frames");

	for (unsigned int i = 0; i < frame_keys.size(); i++) {
		string parent_frame;
		string frame_name;
		Vector3f parent_translation;
		Matrix33f parent_rotation;

		ostringstream frame_path;
		frame_path << "frames." << frame_keys[i];

		if (!lua_read_frame (
					L,
					frame_path.str(),
					frame_name,
					parent_frame,
					parent_translation,
					parent_rotation)) {
			cerr << "Error reading frame " << frame_keys[i] << "." << endl;
			if (strict)
				abort();

			return false;
		}

		Matrix44f parent_transform = Matrix44f::Identity(); 
		parent_transform.block<3,3>(0,0) = parent_rotation.transpose();
		parent_transform.block<1,3>(3,0) = parent_translation.transpose();
		addFrame (parent_frame, frame_name, parent_transform);

		string visuals_path = frame_path.str() + ".visuals";
		if (!value_exists (L, visuals_path)) {
			continue;
		} else {
			vector<string> visuals_keys = get_keys (L, visuals_path);

			for (unsigned int j = 0; j < visuals_keys.size(); j++) {
				string visual_path = visuals_path + string (".") + string(visuals_keys[j]);

				string segment_name;
				Vector3f dimensions (0., 0., 0.);
				Vector3f scale (1., 1., 1.);
				Vector3f color (1., 1., 1.);
				string mesh_filename;
				Vector3f translate (0., 0., 0.);
				Vector3f mesh_center (0., 0., 0.);

				if (!lua_read_visual_info (
							L,
							visual_path,
							segment_name,
							dimensions,
							scale,
							color,
							mesh_filename,
							translate,
							mesh_center)) {
					cerr << "Error reading mesh information " << visual_path << "." << endl;

					if (strict)
						abort();

					return false;
				}

				addSegment (frame_name, segment_name, dimensions,
						scale, color, mesh_filename, translate, mesh_center);
			}
		}
	}

	return true;
}
	
struct ColumnInfo {
	ColumnInfo() :
		frame (FramePtr()),
		type (TypeUnknown),
		axis (AxisUnknown),
		is_time_column (false),
		is_empty (false),
		is_radian (false)
	{}
	enum TransformType {
		TypeUnknown = 0,
		TypeRotation,
		TypeTranslation,
		TypeScale,
	};
	enum AxisName {
		AxisUnknown = 0,
		AxisX,
		AxisY,
		AxisZ,
		AxisMX,
		AxisMY,
		AxisMZ
	};
	FramePtr frame;
	TransformType type;
	AxisName axis;

	bool is_time_column;
	bool is_empty;
	bool is_radian;
};

struct AnimationKeyPoses {
	float timestamp;
	std::vector<ColumnInfo> columns;
	typedef std::map<FramePtr, FramePose> FramePoseMap;
	FramePoseMap frame_poses;
	
	void clearFramePoses() {
		frame_poses.clear();
	}
	bool setValue (int column_index, float value, bool strict = true) {
		assert (column_index <= columns.size());
		ColumnInfo col_info = columns[column_index];

		if (col_info.is_time_column) {
			timestamp = value;
			return true;
		}
		if (col_info.is_empty) {
			return true;
		}

		FramePtr frame = col_info.frame;

		if (frame_poses.find(frame) == frame_poses.end()) {
			// create new frame and insert it
			frame_poses[frame] = FramePose();
		}

		if (col_info.type == ColumnInfo::TypeRotation) {
			if (col_info.axis == ColumnInfo::AxisX) {
				frame_poses[frame].rotation[0] = value;
			}
			if (col_info.axis == ColumnInfo::AxisY) {
				frame_poses[frame].rotation[1] = value;
			}
			if (col_info.axis == ColumnInfo::AxisZ) {
				frame_poses[frame].rotation[2] = value;
			}
			if (col_info.axis == ColumnInfo::AxisMX) {
				frame_poses[frame].rotation[0] = -value;
			}
			if (col_info.axis == ColumnInfo::AxisMY) {
				frame_poses[frame].rotation[1] = -value;
			}
			if (col_info.axis == ColumnInfo::AxisMZ) {
				frame_poses[frame].rotation[2] = -value;
			}
		} else if (col_info.type == ColumnInfo::TypeTranslation) {
			if (col_info.axis == ColumnInfo::AxisX) {
				frame_poses[frame].translation[0] = value;
			}
			if (col_info.axis == ColumnInfo::AxisY) {
				frame_poses[frame].translation[1] = value;
			}
			if (col_info.axis == ColumnInfo::AxisZ) {
				frame_poses[frame].translation[2] = value;
			}	
			if (col_info.axis == ColumnInfo::AxisMX) {
				frame_poses[frame].translation[0] = -value;
			}
			if (col_info.axis == ColumnInfo::AxisMY) {
				frame_poses[frame].translation[1] = -value;
			}
			if (col_info.axis == ColumnInfo::AxisMZ) {
				frame_poses[frame].translation[2] = -value;
			}	
		} else if (col_info.type == ColumnInfo::TypeScale) {
			if (col_info.axis == ColumnInfo::AxisX) {
				frame_poses[frame].scaling[0] = value;
			}
			if (col_info.axis == ColumnInfo::AxisY) {
				frame_poses[frame].scaling[1] = value;
			}
			if (col_info.axis == ColumnInfo::AxisZ) {
				frame_poses[frame].scaling[2] = value;
			}	
			if (col_info.axis == ColumnInfo::AxisMX) {
				frame_poses[frame].scaling[0] = -value;
			}
			if (col_info.axis == ColumnInfo::AxisMY) {
				frame_poses[frame].scaling[1] = -value;
			}
			if (col_info.axis == ColumnInfo::AxisMZ) {
				frame_poses[frame].scaling[2] = -value;
			}	
		} else {
			cerr << "Error: invalid column info type: " << col_info.type << ". Something really weird happened!" << endl;

			if (strict)
				exit (1);

			return false;
		}

		return true;
	}
	void updateTimeValues () {
		for (FramePoseMap::iterator pose_iter = frame_poses.begin(); pose_iter != frame_poses.end(); pose_iter++) {
			pose_iter->second.timestamp = timestamp;
		}
	}
};

bool MeshupModel::loadAnimationFromFile (const char* filename, bool strict) {
	ifstream file_in (filename);

	if (!file_in) {
		cerr << "Error opening animation file " << filename << "!";

		if (strict)
			exit (1);

		return false;
	}

	cout << "Loading animation " << filename << endl;

	string line;

	bool column_section = false;
	bool data_section = false;
	int column_index = 0;
	int line_number = 0;
	AnimationKeyPoses animation_keyposes;

	while (!file_in.eof()) {
		getline (file_in, line);
		line_number++;
	
		line = strip_comments (strip_whitespaces( (line)));
		
		// skip lines with no information
		if (line.size() == 0)
			continue;

		if (line.substr (0, string("COLUMNS:").size()) == "COLUMNS:") {
			column_section = true;

			// we set it to -1 and can then easily increasing the value
			column_index = -1;
			continue;
		}

		if (line.substr (0, string("DATA:").size()) == "DATA:") {
			column_section = false;
			data_section = true;
			continue;
		}

		if (column_section) {
			// do columny stuff
			// cout << "COLUMN:" << line << endl;

			std::vector<string> elements = tokenize(line, ", \t\n\r");
			for (int ei = 0; ei < elements.size(); ei++) {
				// skip elements that had multiple spaces in them
				if (elements[ei].size() == 0)
					continue;

				// it's safe to increase the column index here, as we did
				// initialize it with -1
				column_index++;

				string column_def = strip_whitespaces(elements[ei]);
				// cout << "  E: " << column_def << endl;

				if (tolower(column_def) == "time") {
					ColumnInfo column_info;
					column_info.is_time_column = true;
					animation_keyposes.columns.push_back(column_info);
					// cout << "Setting time column to " << column_index << endl;
					continue;
				}
				if (tolower(column_def) == "empty") {
					ColumnInfo column_info;
					column_info.is_empty = true;
					animation_keyposes.columns.push_back(column_info);
					continue;
				}
				
				std::vector<string> spec = tokenize(column_def, ":");
				if (spec.size() < 3 || spec.size() > 4) {
					cerr << "Error: parsing column definition '" << column_def << "' in " << filename << " line " << line_number << endl;

					if (strict)
						exit(1);

					return false;
				}

				// find the frame
				FramePtr frame = findFrame (strip_whitespaces(spec[0]).c_str());
				if (frame == NULL) {
					cerr << "Error: Unknown frame '" << spec[0] << "' in " << filename << " line " << line_number << endl;

					if (strict)
						exit (1);

					return false;
				}

				// the transform type
				string type_str = tolower(strip_whitespaces(spec[1]));
				ColumnInfo::TransformType type = ColumnInfo::TypeUnknown;
				if (type_str == "rotation"
						|| type_str == "r")
					type = ColumnInfo::TypeRotation;
				else if (type_str == "translation"
						|| type_str == "t")
					type = ColumnInfo::TypeTranslation;
				else if (type_str == "scale"
						|| type_str == "s")
					type = ColumnInfo::TypeScale;
				else {
					cerr << "Error: Unknown transform type '" << spec[1] << "' in " << filename << " line " << line_number << endl;
					
					if (strict)
						exit (1);

					return false;
				}

				// and the axis
				string axis_str = tolower(strip_whitespaces(spec[2]));
				ColumnInfo::AxisName axis_name;
				if (axis_str == "x")
					axis_name = ColumnInfo::AxisX;
				else if (axis_str == "y")
					axis_name = ColumnInfo::AxisY;
				else if (axis_str == "z")
					axis_name = ColumnInfo::AxisZ;
				else if (axis_str == "-x")
					axis_name = ColumnInfo::AxisMX;
				else if (axis_str == "-y")
					axis_name = ColumnInfo::AxisMY;
				else if (axis_str == "-z")
					axis_name = ColumnInfo::AxisMZ;
				else {
					cerr << "Error: Unknown axis name '" << spec[2] << "' in " << filename << " line " << line_number << endl;

					if (strict)
						exit (1);

					return false;
				}

				bool unit_is_radian = false;
				if (spec.size() == 4) {
					string unit_str = tolower(strip_whitespaces(spec[3]));
					if (unit_str == "r" || unit_str == "rad" || unit_str == "radians")
						unit_is_radian = true;
				}

				ColumnInfo col_info;
				col_info.frame = frame;
				col_info.type = type;
				col_info.axis = axis_name;
				col_info.is_radian = unit_is_radian;

				// cout << "Adding column " << column_index << " " << frame->name << ", " << type << ", " << axis_name << " radians = " << col_info.is_radian << endl;
				animation_keyposes.columns.push_back(col_info);
			}

			continue;
		}

		if (data_section) {
			// cout << "DATA  :" << line << endl;
			// parse the DOF description and set the column info in
			// animation_keyposes

			// Data part:
			// columns have been read
			std::vector<string> columns = tokenize (line);
			assert (columns.size() >= animation_keyposes.columns.size());

			// we update all the frame_poses. Once we're done, we add all poses
			// to the given time and clear all frame poses again.
			animation_keyposes.clearFramePoses();

			for (int ci = 0; ci < animation_keyposes.columns.size(); ci++) {
				// parse each column value and submit it to animation_keyposes
				float value;
				istringstream value_stream (columns[ci]);
				value_stream >> value;
				
				// handle radian
				if (animation_keyposes.columns[ci].type==ColumnInfo::TypeRotation && animation_keyposes.columns[ci].is_radian) {
					value *= 180. / M_PI;
				}
				
				// cout << "  col value " << ci << " = " << value << endl;
				animation_keyposes.setValue (ci, value, strict);
			}

			// dispatch the time information to all frame poses
			animation_keyposes.updateTimeValues();

			AnimationKeyPoses::FramePoseMap::iterator frame_pose_iter = animation_keyposes.frame_poses.begin();
			while (frame_pose_iter != animation_keyposes.frame_poses.end()) {
				// call addFramePose()
				FramePtr frame = frame_pose_iter->first;
				FramePose pose = frame_pose_iter->second;

				// cout << "addFramePose("
				// 	<< "  " << frame->name << endl
				// 	<< "  " << pose.timestamp << endl
				// 	<< "  " << pose.translation.transpose() << endl
				// 	<< "  " << pose.rotation.transpose() << endl
				// 	<< "  " << pose.scaling.transpose() << endl;

				addFramePose (frame->name.c_str(),
						pose.timestamp,
						pose.translation,
						pose.rotation,
						pose.scaling
						);

				frame_pose_iter++;
			}
			continue;
		}
	}

	animation_filename = filename;

	return true;
}
