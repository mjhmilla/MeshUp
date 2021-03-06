#include <UnitTest++.h>

#include "Model.h"
#include "Animation.h"
#include "SimpleMath/SimpleMathGL.h"

#include <iostream>

using namespace std;
using namespace SimpleMath::GL;

const float TEST_PREC = 1.0e-6;

struct ModelFixture {
	ModelFixture() {
		model = MeshupModelPtr (new MeshupModel());
		model->skip_vbo_generation = true;
		model->meshmap.insert (make_pair<std::string, MeshPtr>("M1", MeshPtr (new MeshVBO())));
		model->addFrame("ROOT", "UPPERARM", SimpleMath::GL::TranslateMat44 (0.f, 1.f, 0.f));
		model->addSegment("UPPERARM",
			MeshPtr(new MeshVBO),
			Vector3f (1.1, 1.2, 1.3), // dimensions
			Vector3f (1.f, 1.f, 1.f), // color
			Vector3f (3.1, 3.2, 3.3), // translate
			Quaternion (0.f, 1.f, 0.f, 0.f), // rotate
			Vector3f (2.1, 2.2, 2.3), // scale
			Vector3f (4.1, 4.2, 4.3) // mesh_center
			);

		animation = AnimationPtr (new Animation());
	}

	MeshupModelPtr model;
	AnimationPtr animation;
	FrameConfig configuration;
};

TEST_FIXTURE (ModelFixture, TestLongEulerInterpolation) {
	std::vector<StateInfo> states;
	std::vector<VectorNd > values;

	StateInfo time_column;
	time_column.is_time_column = true;

	StateInfo upperarm_r_z;
	upperarm_r_z.frame_name = "UPPERARM";
	upperarm_r_z.axis = StateInfo::AxisTypeZ;
	upperarm_r_z.type = StateInfo::TransformTypeRotation;;

	StateInfo upperarm_r_y;
	upperarm_r_y.frame_name = "UPPERARM";
	upperarm_r_y.axis = StateInfo::AxisTypeY;
	upperarm_r_y.type = StateInfo::TransformTypeRotation;;

	StateInfo upperarm_r_x;
	upperarm_r_x.frame_name = "UPPERARM";
	upperarm_r_x.axis = StateInfo::AxisTypeX;
	upperarm_r_x.type = StateInfo::TransformTypeRotation;;

	states.push_back (time_column);
	states.push_back (upperarm_r_z);
	states.push_back (upperarm_r_y);
	states.push_back (upperarm_r_x);

	VectorNd value_row (VectorNd::Zero (4));
	values.push_back(value_row);
	value_row[0] = 5.f;
	value_row[1] = 200.f;
	value_row[2] = 120.f;
	value_row[3] = 200.f;
	values.push_back(value_row);

	animation->duration = 5.;
	animation->state_descriptor.states = states;
	animation->raw_values = values;

	KeyFrame frame_first = animation->getKeyFrameAtTime (0.f);	
	KeyFrame frame_mid = animation->getKeyFrameAtTime (2.5f);
	KeyFrame frame_last = animation->getKeyFrameAtTime (5.f);

	SimpleMath::GL::Quaternion quat_first = frame_first.transformations["UPPERARM"].rotation_quaternion;
	SimpleMath::GL::Quaternion quat_mid = frame_mid.transformations["UPPERARM"].rotation_quaternion;
	SimpleMath::GL::Quaternion quat_last = frame_last.transformations["UPPERARM"].rotation_quaternion;
	
	UpdateModelFromAnimation (model, animation, 2.5);

	FramePtr upper_frame = model->findFrame("UPPERARM");

	CHECK_CLOSE (1.f, quat_mid.squaredNorm(), 1.0e-8);
}
