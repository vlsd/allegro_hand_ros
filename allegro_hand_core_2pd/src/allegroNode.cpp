/*
 * allegroNode.cpp
 *
 *  Created on: Nov 14, 2012
 *  Authors: Alex ALSPACH, Seungsu KIM
 */
 
// CONTROL LOOP TEMPLATE 
// Using  timer callback  

// For the most basic torque control algorithm, 
// you need only add code where it says:
	// =============================== //
	// = COMPUTE control torque here = //
	// =============================== //	
// in the timer callback below. 
// READ, COMPUTE, and WRITE and PUBLISH
// are contrained within this callback.
 
#include <iostream>
#include <boost/thread/thread.hpp>
#include <boost/date_time.hpp>
#include <boost/thread/locks.hpp>

#include "ros/ros.h"
#include "ros/service.h"
#include "ros/service_server.h"
#include "sensor_msgs/JointState.h"
#include "std_msgs/String.h"
#include "std_msgs/Float32.h"

#include <XmlRpcValue.h>

#include <stdio.h>
#include <math.h>
#include <iostream>
#include <string>

#include "controlAllegroHand.h"


#define RADIANS_TO_DEGREES(radians) ((radians) * (180.0 / M_PI))
#define DEGREES_TO_RADIANS(angle) ((angle) / 180.0 * M_PI)


// Topics
#define JOINT_STATE_TOPIC "/allegroHand/joint_states"
#define JOINT_CMD_TOPIC "/allegroHand/joint_cmd"
#define EXT_CMD_TOPIC "/allegroHand/external_cmd"

double current_position[DOF_JOINTS] 			= {0.0};
double previous_position[DOF_JOINTS]			= {0.0};

double current_position_filtered[DOF_JOINTS] 	= {0.0};
double previous_position_filtered[DOF_JOINTS]	= {0.0};

double current_velocity[DOF_JOINTS] 			= {0.0};
double previous_velocity[DOF_JOINTS] 			= {0.0};
double current_velocity_filtered[DOF_JOINTS] 	= {0.0};

double desired_position[DOF_JOINTS]				= {0.0};
double desired_torque[DOF_JOINTS] 				= {0.0};

double k_p[DOF_JOINTS] 				= { 600.0,  600.0,  600.0, 1000.0,  // default P gains
										600.0,  600.0,  600.0, 1000.0,
										600.0,  600.0,  600.0, 1000.0,
									   1000.0, 1000.0, 1000.0,  600.0 };

double k_d[DOF_JOINTS] 				= {  15.0,   20.0,   15.0,   15.0,  // default D gains
										 15.0,   20.0,   15.0,   15.0,
										 15.0,   20.0,   15.0,   15.0,
										 30.0,   20.0,   20.0,   15.0 };

double home_pose[DOF_JOINTS]		= {   0.0,  -10.0,   45.0,   45.0,  // default (home) position
										  0.0,  -10.0,   45.0,   45.0,
										  5.0,   -5.0,   50.0,   45.0,
					   				     60.0,   25.0,   15.0,   45.0 };

std::string pGainParams[DOF_JOINTS] = {	"/gains/p/j00", "/gains/p/j01", "/gains/p/j02", "/gains/p/j03", 
										"/gains/p/j10", "/gains/p/j11", "/gains/p/j12", "/gains/p/j13",
										"/gains/p/j20", "/gains/p/j21", "/gains/p/j22", "/gains/p/j23", 
										"/gains/p/j30", "/gains/p/j31", "/gains/p/j32", "/gains/p/j33", };

std::string dGainParams[DOF_JOINTS] = {	"/gains/d/j00", "/gains/d/j01", "/gains/d/j02", "/gains/d/j03", 
										"/gains/d/j10", "/gains/d/j11", "/gains/d/j12", "/gains/d/j13",
										"/gains/d/j20", "/gains/d/j21", "/gains/d/j22", "/gains/d/j23", 
										"/gains/d/j30", "/gains/d/j31", "/gains/d/j32", "/gains/d/j33", };

std::string jointNames[DOF_JOINTS] 	= {    "joint_0.0",    "joint_1.0",    "joint_2.0",   "joint_3.0" , 
										   "joint_4.0",    "joint_5.0",    "joint_6.0",   "joint_7.0" , 
									  	   "joint_8.0",    "joint_9.0",    "joint_10.0",  "joint_11.0", 
										   "joint_12.0",    "joint_13.0",  "joint_14.0",  "joint_15.0" };


int frame = 0;

// Flags
int lEmergencyStop = 0;

boost::mutex *mutex;

// ROS Messages
ros::Publisher joint_state_pub;
ros::Subscriber joint_cmd_sub;		// handles external joint command (eg. sensor_msgs/JointState)
ros::Subscriber external_cmd_sub;	// handles any other type of eternal command (eg. std_msgs/String)
sensor_msgs::JointState msgJoint;
std::string  ext_cmd;

// ROS Time
ros::Time tstart;
ros::Time tnow;
double secs;
double dt;

// Initialize CAN device		
controlAllegroHand *canDevice;







// Called when a desired joint position message is received
void SetjointCallback(const sensor_msgs::JointState& msg)
{
	mutex->lock();
	for(int i=0;i<DOF_JOINTS;i++) desired_position[i] = msg.position[i];
	mutex->unlock();	
}


// Called when an external (string) message is received
void extCmdCallback(const std_msgs::String::ConstPtr& msg)
{
	ROS_INFO("CTRL: Processing external command");
	ext_cmd = msg->data.c_str();

	// Compare the message received to an expected input
	if (ext_cmd.compare("comparisonString") == 0)
	{
		// Do something
	}    
	else
	{
		// Do something else
	}	
}




/*

// In case of the Allegro Hand, this callback is processed
// every 0.003 seconds
void timerCallback(const ros::TimerEvent& event)
{


	

} // end timerCallback

*/






int main(int argc, char** argv)
{

	using namespace std;
	
	ros::init(argc, argv, "allegro_hand_core");
	ros::Time::init();
	
	ros::NodeHandle nh;
	
	// Setup timer callback (ALLEGRO_CONTROL_TIME_INTERVAL = 0.003)
	//ros::Timer timer = nh.createTimer(ros::Duration(0.003), timerCallback);
	ros::Rate rate(1./ALLEGRO_CONTROL_TIME_INTERVAL);
	
	mutex = new boost::mutex();

	// Publisher and Subscribers
	joint_state_pub = nh.advertise<sensor_msgs::JointState>(JOINT_STATE_TOPIC, 3);
	joint_cmd_sub = nh.subscribe(JOINT_CMD_TOPIC, 3, SetjointCallback);
	external_cmd_sub = nh.subscribe(EXT_CMD_TOPIC, 1, extCmdCallback);
	
	// Create arrays 16 long for each of the four joint state components
	msgJoint.position.resize(DOF_JOINTS);
	msgJoint.velocity.resize(DOF_JOINTS);
	msgJoint.effort.resize(DOF_JOINTS);
	msgJoint.name.resize(DOF_JOINTS);


	// Joint names (for use with joint_state_publisher GUI - matches URDF)
	for(int i=0; i<DOF_JOINTS; i++)	msgJoint.name[i] = jointNames[i];	
	
	
	// Get Allegro Hand information from parameter server
	// This information is found in the Hand-specific "zero.yaml" file from the allegro_hand_description package	
	string robot_name, whichHand, manufacturer, origin, serial, version;
	ros::param::get("~hand_info/robot_name",robot_name);
	ros::param::get("~hand_info/which_hand",whichHand);
	ros::param::get("~hand_info/manufacturer",manufacturer);
	ros::param::get("~hand_info/origin",origin);
	ros::param::get("~hand_info/serial",serial);
	ros::param::get("~hand_info/version",version);


	// set gains via gains.yaml or to defaul values
	if (ros::param::has("~gains"))
	{
		ROS_INFO("CTRL: PD gains loaded from param server.");
		for(int i=0; i<DOF_JOINTS; i++)
		{
			ros::param::get(pGainParams[i], k_p[i]);
			ros::param::get(dGainParams[i], k_d[i]);
			printf("%f ", k_p[i]);
		}
		printf("\n");
	}
	else
	{
		// gains will be loaded every control iteration
		ROS_WARN("CTRL: PD gains not loaded.\nCheck launch file is loading /parameters/gains.yaml\nLoading default PD gains...");
	}


	// set initial position via initial_position.yaml or to defaul values
	if (ros::param::has("~initial_position"))
	{
		ROS_INFO("CTRL: Initial Pose loaded from param server.");
		
/*  ================================= 
    =       LOAD VALUES HERE        =   
    ================================= */
    
	}
	else
	{
		ROS_WARN("Initial postion not loaded.\nCheck launch file is loading /parameters/initialPose.yaml\nLoading Home position instead...");
		// Home position
		for(int i=0; i<DOF_JOINTS; i++)	desired_position[i] = DEGREES_TO_RADIANS(home_pose[i]);										
	}


	// Initialize CAN device
	canDevice = new controlAllegroHand();
	canDevice->init();
	usleep(3000);
	
	// Dump Allegro Hand information to the terminal	
	cout << endl << endl << robot_name << " " << version << endl << serial << " (" << whichHand << ")" << endl << manufacturer << endl << origin << endl << endl;

	// Start ROS time
	tstart = ros::Time::now();

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


	while(ros::ok())
	{


	// Calculate loop time;
	tnow = ros::Time::now();
	dt = 1e-9*(tnow - tstart).nsec;
	tstart = tnow;
	
	if ((1/dt)>400)
		printf("%f\n",(1/dt-333.33333));
	
		
	// save last iteration info
	for(int i=0; i<DOF_JOINTS; i++)
	{
		previous_position[i] = current_position[i];
		previous_position_filtered[i] = current_position_filtered[i];
		previous_velocity[i] = current_velocity[i];
	}
		
		
/*  ================================= 
    =       CAN COMMUNICATION       = 
    ================================= */	
	canDevice->setTorque(desired_torque);		// WRITE joint torque
	lEmergencyStop = canDevice->update(); 		// Returns -1 in case of an error
	canDevice->getJointInfo(current_position);	// READ current joint positions

		
		
				
	// Stop program and shutdown node when Allegro Hand is switched off
	if( lEmergencyStop < 0 )
	{
		ROS_ERROR("\n\nAllegro Hand Node is Shutting Down! (Emergency Stop)\n");
		ros::shutdown();
	}



/*  ================================= 
    =       LOWPASS FILTERING       =   
    ================================= */
    //double alpha = 0.6;
    //double beta = 0.198;	
	for(int i=0; i<DOF_JOINTS; i++)    
	{
    	current_position_filtered[i] = (0.6*current_position_filtered[i]) + (0.198*previous_position[i]) + (0.198*current_position[i]);
		current_velocity[i] = (current_position_filtered[i] - previous_position_filtered[i]) / dt;
		current_velocity_filtered[i] = (0.6*current_velocity_filtered[i]) + (0.198*previous_velocity[i]) + (0.198*current_velocity[i]);
	}
	
/*  ================================= 
    =        POSITION CONTROL       =   
    ================================= */
    if(frame>20) // give the low pass filters 20 iterations to build up good data.
    {	
		for(int i=0; i<DOF_JOINTS; i++)    
		{
			desired_torque[i] = k_p[i]*(desired_position[i]-current_position_filtered[i]) - k_d[i]*current_velocity_filtered[i];
			desired_torque[i] = desired_torque[i]/800.0;
		}
	}		


/*  ================================= 
    =   ADD VELOCITY SAT. CONTRL    =   
    ================================= */

		
	// PUBLISH current position, velocity and effort (torque)
	msgJoint.header.stamp 		= tnow;	
	for(int i=0; i<DOF_JOINTS; i++)
	{
		msgJoint.position[i] 	= current_position[i];
		msgJoint.velocity[i] 	= current_velocity[i];
		msgJoint.effort[i] 		= desired_torque[i];
	}
	joint_state_pub.publish(msgJoint);
		
	frame++;







			ros::spinOnce();
			rate.sleep();	
	}

	// Starts control loop, message pub/subs and all other callbacks
	//ros::spin();			

	// Clean shutdown: shutdown node, shutdown can, be polite.
	nh.shutdown();
	delete canDevice;
	printf("\nAllegro Hand Node has been shut down. Bye!\n\n");
	return 0;
	
}

