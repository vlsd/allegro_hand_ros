/*
 * allegroNode.cpp
 *
 *  Created on: Nov 14, 2012
 *  Authors: Alex ALSPACH, Seungsu KIM
 */
 
// GRASP LIBRARY INTERFACE
// Using  sleep function  
 
#include <iostream>
#include <boost/thread/thread.hpp>
#include <boost/date_time.hpp>
#include <boost/thread/locks.hpp>

#include "ros/ros.h"
#include "ros/service.h"
#include "ros/service_server.h"
#include "sensor_msgs/JointState.h"
#include "std_msgs/String.h"

#include <stdio.h>
#include <iostream>
#include <string>

#include "BHand/BHand.h"
#include "controlAllegroHand.h"

// Topics
#define JOINT_STATE_TOPIC "/allegroHand/joint_states"
#define JOINT_CMD_TOPIC "/allegroHand/joint_cmd"
#define LIB_CMD_TOPIC "/allegroHand/lib_cmd"


double desired_position[DOF_JOINTS] 			= {0.0};
double current_position[DOF_JOINTS] 			= {0.0};
double previous_position[DOF_JOINTS] 			= {0.0};
double current_position_filtered[DOF_JOINTS] 	= {0.0};
double previous_position_filtered[DOF_JOINTS]	= {0.0};

double current_velocity[DOF_JOINTS] 			= {0.0};
double previous_velocity[DOF_JOINTS] 			= {0.0};
double current_velocity_filtered[DOF_JOINTS] 	= {0.0};

double desired_torque[DOF_JOINTS] 				= {0.0};

std::string  lib_cmd;


std::string jointNames[DOF_JOINTS] 	= {    "joint_0.0",    "joint_1.0",    "joint_2.0",   "joint_3.0" , 
										   "joint_4.0",    "joint_5.0",    "joint_6.0",   "joint_7.0" , 
									  	   "joint_8.0",    "joint_9.0",    "joint_10.0",  "joint_11.0", 
										   "joint_12.0",   "joint_13.0",   "joint_14.0",  "joint_15.0" };

int frame = 0;

// Flags
bool lIsBegin = false;
bool kill = false;	
bool pdControl = true;
int lEmergencyStop = 0;

boost::mutex *mutex;

// ROS Messages
ros::Publisher joint_state_pub;
ros::Subscriber joint_cmd_sub;
ros::Subscriber lib_cmd_sub;
sensor_msgs::JointState msgJoint;

// ROS Time
ros::Time tstart;
ros::Time tnow;
double secs;
double dt;

// Initialize BHand 
eMotionType gMotionType = eMotionType_NONE ;
BHand* pBHand = NULL;
//BHand lBHand(eHandType_Right);


// Initialize CAN device		
controlAllegroHand *canDevice;



// Called when a desired joint position message is received
void SetjointCallback(const sensor_msgs::JointState& msg)
{
	// TODO check joint limits
	pdControl=true;

	mutex->lock();
	for(int i=0;i<DOF_JOINTS;i++) desired_position[i] = msg.position[i];
	mutex->unlock();

	pBHand->SetMotionType(eMotionType_JOINT_PD);
}

void libCmdCallback(const std_msgs::String::ConstPtr& msg)
{
ROS_INFO("I heard: [%s]", msg->data.c_str());
lib_cmd = msg->data.c_str();

// If PD Control is commanded, pdControl is set true
// so that the controll loop has access to desired position.
if (lib_cmd.compare("pdControl") == 0)
{
	pBHand->SetMotionType(eMotionType_JOINT_PD);
	pdControl = true;
}    
else
    pdControl = false;  

if (lib_cmd.compare("home") == 0) 
	pBHand->SetMotionType(eMotionType_HOME);
    
if (lib_cmd.compare("ready") == 0) 
	pBHand->SetMotionType(eMotionType_READY);

if (lib_cmd.compare("grasp_3") == 0) 
	pBHand->SetMotionType(eMotionType_GRASP_3);

if (lib_cmd.compare("grasp_4") == 0) 
	pBHand->SetMotionType(eMotionType_GRASP_4);
    
if (lib_cmd.compare("pinch_it") == 0) 
	pBHand->SetMotionType(eMotionType_PINCH_IT);

if (lib_cmd.compare("pinch_mt") == 0) 
	pBHand->SetMotionType(eMotionType_PINCH_MT); 	 

if (lib_cmd.compare("envelop") == 0) 
	pBHand->SetMotionType(eMotionType_ENVELOP); 

if (lib_cmd.compare("off") == 0) 
	pBHand->SetMotionType(eMotionType_NONE);

if (lib_cmd.compare("save") == 0) 
	for(int i=0; i<DOF_JOINTS; i++) desired_position[i] = current_position[i];

/*
eMotionType_NONE,				// motor power off
eMotionType_HOME,				// go to home position
eMotionType_READY,				// ready position for grasping
eMotionType_GRASP_3,			// grasping using 3 fingers
eMotionType_GRASP_4,			// grasping using 4 fingers
eMotionType_PINCH_IT,			// pinching using index finger and thumb
eMotionType_PINCH_MT,			// pinching using middle finger and thumb
eMotionType_ENVELOP,			// enveloping
eMotionType_JOINT_PD,			// joint pd control
eMotionType_OBJECT_MOVING,		//
eMotionType_PRE_SHAPE,			//
*/	

}



int main(int argc, char** argv)
{
	using namespace std;

	ros::init(argc, argv, "allegro_hand_core_grasp");
	ros::Time::init();

	ros::NodeHandle nh;

	// Setup sleep rate
	ros::Rate rate(1./ALLEGRO_CONTROL_TIME_INTERVAL);

	mutex = new boost::mutex();

	// Publisher and Subscribers
	joint_state_pub = nh.advertise<sensor_msgs::JointState>(JOINT_STATE_TOPIC, 3);
	joint_cmd_sub = nh.subscribe(JOINT_CMD_TOPIC, 3, SetjointCallback);
	lib_cmd_sub = nh.subscribe(LIB_CMD_TOPIC, 1000, libCmdCallback);

	// Create arrays 16 long for each of the four joint state components
	msgJoint.position.resize(DOF_JOINTS);
	msgJoint.velocity.resize(DOF_JOINTS);
	msgJoint.effort.resize(DOF_JOINTS);
	msgJoint.name.resize(DOF_JOINTS);

	// Joint names (for use with joint_state_publisher GUI - matches URDF)
	for(int i=0; i<DOF_JOINTS; i++)	msgJoint.name[i] = jointNames[i];	

	
	// Get Allegro Hand information from parameter server
	// This information is found in the Hand-specific "zero.yaml" file from the allegro_hand_description package	
	string robot_name, whichHand, manufacturer, origin, serial;
	double version;
	ros::param::get("~hand_info/robot_name",robot_name);
	ros::param::get("~hand_info/which_hand",whichHand);
	ros::param::get("~hand_info/manufacturer",manufacturer);
	ros::param::get("~hand_info/origin",origin);
	ros::param::get("~hand_info/serial",serial);
	ros::param::get("~hand_info/version",version);

	// Initialize BHand controller
	if (whichHand.compare("left") == 0)
	{
		pBHand = new BHand(eHandType_Left);
		ROS_WARN("CTRL: Left Allegro Hand controller initialized.");
	}
	else
	{
		pBHand = new BHand(eHandType_Right);
		ROS_WARN("CTRL: Right Allegro Hand controller initialized.");
	}
	pBHand->SetTimeInterval(ALLEGRO_CONTROL_TIME_INTERVAL);

	// Initialize CAN device
	canDevice = new controlAllegroHand();
	canDevice->init();
	usleep(3000);
	
	// Dump Allegro Hand information to the terminal	
	cout << endl << endl << robot_name << " v" << version << endl << serial << " (" << whichHand << ")" << endl << manufacturer << endl << origin << endl << endl;
	

	// Initialize torque at zero
	for(int i=0; i<16; i++) desired_torque[i] = 0.0;

	// Set to false for first iteration of control loop (sets PD control at start pose)
	lIsBegin = false;

	// Start ROS time
	tstart = ros::Time::now();




	while(ros::ok())
	{
		// Calculate loop time;
		tnow = ros::Time::now();
		dt = 1e-9*(tnow - tstart).nsec;
		tstart = tnow;

		// save last iteration info
		for(int i=0; i<DOF_JOINTS; i++)
		{
			previous_position[i] = current_position[i];
			previous_position_filtered[i] = current_position_filtered[i];
			previous_velocity[i] = current_velocity[i];
		}

		/* ================================== 
		 =        CAN COMMUNICATION         =   
		 ================================== */
		canDevice->setTorque(desired_torque);
		lEmergencyStop = canDevice->update();
        canDevice->getJointInfo(current_position);
		
		
		/* ================================== 
		 =         LOWPASS FILTERING        =   
		 ================================== */
		for(int i=0; i<DOF_JOINTS; i++)    
		{
			current_position_filtered[i] = (0.6*current_position_filtered[i]) + (0.198*previous_position[i]) + (0.198*current_position[i]);
			current_velocity[i] = (current_position_filtered[i] - previous_position_filtered[i]) / dt;
			current_velocity_filtered[i] = (0.6*current_velocity_filtered[i]) + (0.198*previous_velocity[i]) + (0.198*current_velocity[i]);
		}			


		if( lEmergencyStop < 0 )
		{
			// Stop program when Allegro Hand is switched off
			printf("\n\n\nEMERGENCY STOP.\n\n");
			ros::shutdown();
		}

		// compute control torque using Bhand library
		pBHand->SetJointPosition(current_position_filtered);

		// Run on FIRST iteration (sets PD control of intitial position)
		if( lIsBegin == false ){
			if(frame > 10){
				mutex->lock();
				for(int i=0; i<DOF_JOINTS; i++) desired_position[i] = current_position[i];
				mutex->unlock();

				lIsBegin = true;
				}

				// Start joint position control (Bhand) in initial position
				// Commented out for safety incase the joint offsets or directions are incorrect
				//pBHand->SetMotionType(eMotionType_JOINT_PD);

				// Starts with motors off but encoder data is read and can be visualized
				pBHand->SetMotionType(eMotionType_NONE);

				pBHand->UpdateControl((double)frame*ALLEGRO_CONTROL_TIME_INTERVAL);
				for(int i=0; i<DOF_JOINTS; i++) desired_torque[i] = 0.0;
			}
			else{			

				// BHAND Communication
				// desired joint positions are obtained from subscriber "joint_cmd_sub"
				// or maintatined as the initial positions from program start (PD control)
				// Also, other motions/grasps can be executed (envoked via "lib_cmd_sub")

				// Desired position only necessary if in PD Control mode	
				if (pdControl==true)
					pBHand->SetJointDesiredPosition(desired_position);

				// BHand lib control updated with time stamp	
				pBHand->UpdateControl((double)frame*ALLEGRO_CONTROL_TIME_INTERVAL);

				// Necessary torque obtained from Bhand lib
				pBHand->GetJointTorque(desired_torque);

				// Calculate joint velocity
				for(int i=0; i<DOF_JOINTS; i++)
					current_velocity[i] = (current_position[i] - previous_position[i])/dt;

				// current position, velocity and effort (torque) published
				msgJoint.header.stamp 									= tnow;
				for(int i=0; i<DOF_JOINTS; i++) msgJoint.position[i] 	= current_position_filtered[i];
				for(int i=0; i<DOF_JOINTS; i++) msgJoint.velocity[i] 	= current_velocity_filtered[i];
				for(int i=0; i<DOF_JOINTS; i++) msgJoint.effort[i] 		= desired_torque[i];
				joint_state_pub.publish(msgJoint);
			}

			frame++;	

			ros::spinOnce();
			rate.sleep();	
	}

	// Clean shutdown: shutdown node, shutdown can, be polite.
	nh.shutdown();
	delete canDevice;
	printf("\nBye.\n");
	return 0;
}

