/**
MIT License

Copyright (c) 2018 Brennan Cain and Michail Kalaitzakis (Unmanned Systems and Robotics Lab, University of South Carolina, USA)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * This file implements the dji interface of the dji_pilot node
 * 
 * Author: Michail Kalaitzakis, Brennan Cain
 */

#include "jetyak_uav_utils/dji_pilot.h"

#include <cmath>

#define C_PI (double)3.141592653589793
#define clip(X, LOW, HIGH) (((X) > (HIGH)) ? (HIGH) : ((X) < (LOW)) ? (LOW) : (X))

dji_pilot::dji_pilot(ros::NodeHandle &nh)
{
	// Load autopilot parameters
	loadPilotParameters();
	if (isM100)
		ROS_INFO("Platform set to M100");

	// Set up subscriptions
	extCmdSub = nh.subscribe("behavior_cmd", 10, &dji_pilot::extCallback, this);
	djiRCSub = nh.subscribe("/dji_sdk/rc", 10, &dji_pilot::rcCallback, this);

	// Set up publisher
	controlPub = nh.advertise<sensor_msgs::Joy>("/dji_sdk/flight_control_setpoint_generic", 10);

	// Set up services
	armServ = nh.serviceClient<dji_sdk::DroneArmControl>("/dji_sdk/drone_arm_control");
	taskServ = nh.serviceClient<dji_sdk::DroneTaskControl>("/dji_sdk/drone_task_control");
	sdkCtrlAuthorityServ = nh.serviceClient<dji_sdk::SDKControlAuthority>("/dji_sdk/sdk_control_authority");

	// Set up service servers
	propServServer = nh.advertiseService("prop_enable", &dji_pilot::propServCallback, this);
	takeoffServServer = nh.advertiseService("takeoff", &dji_pilot::takeoffServCallback, this);
	landServServer = nh.advertiseService("land", &dji_pilot::landServCallback, this);

	// Set default values
	rcStickThresh = 0.0;
	autopilotOn = false;
	bypassPilot = false;

	// Initialize RC
	setupRCCallback();
	rcReceived = false;
	panicMode = false;

	// Initialize default command flag
	commandFlag = (DJISDK::VERTICAL_VELOCITY |
		       DJISDK::HORIZONTAL_ANGLE |
		       DJISDK::YAW_RATE |
		       DJISDK::HORIZONTAL_BODY |
		       DJISDK::STABLE_DISABLE);
	
	// Initialize joy command
	extCommand.axes.clear();
	for (int i = 0; i < 4; ++i)
		extCommand.axes.push_back(0);
	
	extCommand.axes.push_back(commandFlag);

	rcCommand.axes.clear();
	for (int i = 0; i < 4; ++i)
		rcCommand.axes.push_back(0);
	
	rcCommand.axes.push_back(commandFlag);

	alarm = new bsc_common::Flasher(250000);
}

dji_pilot::~dji_pilot()
{
	if (autopilotOn)
	{
		// Try to release control
		if (requestControl(0))
			ROS_INFO("Control released back to RC");
	}
	alarm->stopFlash();
	delete alarm;
}

void dji_pilot::loadPilotParameters()
{
	ros::NodeHandle nh_private("~");

	// Platform
	nh_private.param("isM100", isM100, true);

	// RC velocity multiplier
	nh_private.param("rcVelocityMultiplierH", rcVelocityMultiplierH, 1.0);
	nh_private.param("rcVelocityMultiplierV", rcVelocityMultiplierV, 1.0);

	// Horizontal velocity thresholds
	nh_private.param("hVelocityMaxBody", hVelocityMaxBody, 1.0);
	nh_private.param("hVelocityMaxGround", hVelocityMaxGround, 5.0);
	nh_private.param("hAngleRateCmdMax", hAngleRateCmdMax, 5.0 * C_PI / 6.0);
	nh_private.param("hAngleCmdMax", hAngleCmdMax, 0.611);

	// Vertical velocity/position thresholds
	nh_private.param("vVelocityMaxBody", vVelocityMaxBody, 1.0);
	nh_private.param("vVelocityMaxGround", vVelocityMaxGround, 3.0);
	nh_private.param("vPosCmdMax", vPosCmdMax, 30.0);
	nh_private.param("vPosCmdMin", vPosCmdMin, 0.0);
	nh_private.param("vThrustCmdMax", vThrustCmdMax, 1.0);

	// Yaw thresholds
	nh_private.param("yAngleRateMax", yAngleRateMax, 5.0 * C_PI / 6.0);
	nh_private.param("yAngleMax", yAngleMax, C_PI);
}

// Callbacks //
/** extCallback
 * @param msg Joy message containing rpty commands in the axes[0:3] and \
 * axes[4] encoding bools for body frame (0b10) and position cmd (0b01)
 */
void dji_pilot::extCallback(const sensor_msgs::Joy::ConstPtr &msg)
{
	// Check if incoming message is full
	if (msg->axes.size() == 5)
	{
		// Pass the joystick message to the command
		extCommand.axes.clear();
		sensor_msgs::Joy *output = new sensor_msgs::Joy();

		for (int i = 0; i < 4; i++)
			output->axes.push_back(msg->axes[i]);
		output->axes.push_back(buildFlag((JETYAK_UAV_UTILS::Flag)(msg->axes[4])));


		// Clip commands according to flag
		extCommand = adaptiveClipping(*output);
	}
	else
		ROS_WARN("Command received has not the expected format");
}

void dji_pilot::rcCallback(const sensor_msgs::Joy::ConstPtr &msg)
{
	if (!rcReceived)
	{
		rcReceived = true;
		ROS_WARN("RC msg received");
	}

	// Update last RC msg time
	lastRCmsg = ros::Time::now();
	
	// Switch autodji_pilot on/off
	// P mode && Autodji_pilot switch on && Autodji_pilot flag not set
	if (msg->axes[4] == modeFlag && msg->axes[5] == pilotFlag && !autopilotOn && !panicMode)
	{
		if (requestControl(1))
			autopilotOn = true;
	}
	// P mode && Autodji_pilot switch off && Autodji_pilot flag set
	// Not P mode && Autodji_pilot flag set
	else if ((msg->axes[4] == modeFlag && msg->axes[5] != pilotFlag && autopilotOn) ||
					 (msg->axes[4] != modeFlag && autopilotOn))
	{
		if (requestControl(0))
			autopilotOn = false;
	}

	// If it is on P mode and the autodji_pilot is on check if the RC is being used
	if (msg->axes[4] == modeFlag && autopilotOn)
	{
		if (std::abs(msg->axes[0]) > rcStickThresh || std::abs(msg->axes[1]) > rcStickThresh ||
				std::abs(msg->axes[2]) > rcStickThresh || std::abs(msg->axes[3]) > rcStickThresh)
		{
			rcCommand.axes[0]=msg->axes[0] * rcVelocityMultiplierH;	// Roll
			rcCommand.axes[1]=msg->axes[1] * rcVelocityMultiplierH; // Pitch
			rcCommand.axes[2]=msg->axes[3] * rcVelocityMultiplierV;	// Altitude
			rcCommand.axes[3]=-msg->axes[2];						// Yaw

			bypassPilot = true;
		}
	}
}

bool dji_pilot::propServCallback(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
{
	if (autopilotOn)
	{
		dji_sdk::DroneArmControl srv;
		srv.request.arm = req.data;
		armServ.call(srv);
		res.success = srv.response.result;
		return true;
	}
	else
		return false;
}

bool dji_pilot::landServCallback(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
{
	if (autopilotOn)
	{
		dji_sdk::DroneTaskControl srv;
		srv.request.task = 6; // landing
		taskServ.call(srv);
		res.success = srv.response.result;
		return true;
	}
	else
		return false;
}

bool dji_pilot::takeoffServCallback(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
{
	if (autopilotOn)
	{
		dji_sdk::DroneTaskControl srv;
		srv.request.task = 4; // takeoff
		taskServ.call(srv);
		res.success = srv.response.result;
		return true;
	}
	else
		return false;
}

// Private //
void dji_pilot::setupRCCallback()
{
	if (isM100)
	{
		modeFlag = 8000;
		pilotFlag = -10000;
	}
	else
	{
		modeFlag = 1;
		pilotFlag = 1;
	}
}

bool dji_pilot::requestControl(int requestFlag)
{
	// Create control request and transmit it to vehicle
	dji_sdk::SDKControlAuthority ctrlAuthority;
	ctrlAuthority.request.control_enable = requestFlag;

	// Request control
	sdkCtrlAuthorityServ.call(ctrlAuthority);

	if (!ctrlAuthority.response.result)
	{
		ROS_ERROR("Could not switch control");
		return false;
	}
	else
	{
		if (requestFlag)
			ROS_INFO("Control of vehicle is obtained");
		else
			ROS_INFO("Released vehicle control");
	}

	return true;
}

sensor_msgs::Joy dji_pilot::adaptiveClipping(sensor_msgs::Joy msg)
{
	// Create command buffer
	sensor_msgs::Joy cmdBuffer;
	cmdBuffer.axes.clear();

	for (int i = 0; i < 4; ++i)
		cmdBuffer.axes.push_back(0);

	uint8_t flag = msg.axes[4];
	cmdBuffer.axes.push_back(flag);

	// Coordinate frame
	double hVelcmdMax, vVelcmdMax;
	if ((flag & DJISDK::HORIZONTAL_BODY) == DJISDK::HORIZONTAL_BODY)
	{
		hVelcmdMax = hVelocityMaxBody;
		vVelcmdMax = vVelocityMaxBody;
	}
	else
	{
		hVelcmdMax = hVelocityMaxGround;
		vVelcmdMax = vVelocityMaxGround;
	}

	// Horizontal Logic
	if ((flag & DJISDK::HORIZONTAL_ANGULAR_RATE) == DJISDK::HORIZONTAL_ANGULAR_RATE)
	{
		cmdBuffer.axes[0] = clip(msg.axes[0], -hAngleRateCmdMax, hAngleRateCmdMax);
		cmdBuffer.axes[1] = clip(msg.axes[1], -hAngleRateCmdMax, hAngleRateCmdMax);
	}
	else if ((flag & DJISDK::HORIZONTAL_POSITION) == DJISDK::HORIZONTAL_POSITION)
	{
		cmdBuffer.axes[0] = msg.axes[0];
		cmdBuffer.axes[1] = msg.axes[1];
	}
	else if ((flag & DJISDK::HORIZONTAL_VELOCITY) == DJISDK::HORIZONTAL_VELOCITY)
	{
		cmdBuffer.axes[0] = clip(msg.axes[0], -hVelcmdMax, hVelcmdMax);
		cmdBuffer.axes[1] = clip(msg.axes[1], -hVelcmdMax, hVelcmdMax);
	}
	else
	{
		cmdBuffer.axes[0]-=.0105;
		cmdBuffer.axes[1]-=.0105;
		cmdBuffer.axes[0] = clip(msg.axes[0], -hAngleCmdMax, hAngleCmdMax);
		cmdBuffer.axes[1] = clip(msg.axes[1], -hAngleCmdMax, hAngleCmdMax);
	}

	// Vertical Logic
	if ((flag & DJISDK::VERTICAL_THRUST) == DJISDK::VERTICAL_THRUST)
		cmdBuffer.axes[2] = clip(msg.axes[2], 0, vThrustCmdMax);
	else if ((flag & DJISDK::VERTICAL_POSITION) == DJISDK::VERTICAL_POSITION)
		cmdBuffer.axes[2] = clip(msg.axes[2], vPosCmdMin, vPosCmdMax);
	else
		cmdBuffer.axes[2] = clip(msg.axes[2], -vVelcmdMax, vVelcmdMax);

	// Yaw Logic
	if ((flag & DJISDK::YAW_RATE) == DJISDK::YAW_RATE)
		cmdBuffer.axes[3] = clip(msg.axes[3], -yAngleRateMax, yAngleRateMax);
	else
		cmdBuffer.axes[3] = clip(msg.axes[3], -yAngleRateMax, yAngleRateMax);

	return cmdBuffer;
}

uint8_t dji_pilot::buildFlag(JETYAK_UAV_UTILS::Flag flag)
{
	uint8_t base = 0;

	switch (flag)
	{
	case JETYAK_UAV_UTILS::WORLD_POS:
	{
		base = DJISDK::HORIZONTAL_POSITION | DJISDK::VERTICAL_POSITION | DJISDK::YAW_ANGLE | DJISDK::HORIZONTAL_GROUND | DJISDK::STABLE_ENABLE;
		break;
	}
	case JETYAK_UAV_UTILS::WORLD_RATE:
	{
		base = DJISDK::HORIZONTAL_VELOCITY | DJISDK::VERTICAL_VELOCITY | DJISDK::YAW_RATE | DJISDK::HORIZONTAL_GROUND | DJISDK::STABLE_DISABLE;
		break;
	}
	case JETYAK_UAV_UTILS::LQR:
	{
		base = DJISDK::HORIZONTAL_ANGLE | DJISDK::VERTICAL_VELOCITY | DJISDK::YAW_RATE | DJISDK::HORIZONTAL_BODY | DJISDK::STABLE_DISABLE;
		break;
	}
	}

	return base;
}

// Public //
void dji_pilot::publishCommand()
{
	if (autopilotOn)
	{
		// Prepare command
		sensor_msgs::Joy djiCommand;

		if (bypassPilot)
			djiCommand = rcCommand;
		else
			djiCommand = extCommand;

		// Get time
		ros::Time time = ros::Time::now();
		djiCommand.header.stamp = time;

		// Publish command
		controlPub.publish(djiCommand);

		// Reset bypass flag
		bypassPilot = false;
	}
}

bool dji_pilot::checkRCconnection()
{
	if (panicMode)
	{
		// Release control
		if (autopilotOn) {
			if (requestControl(0))
				autopilotOn = false;
		}

		// TO DO: Sound alarm
		ROS_WARN("SDK lost connection to RC: PANIC!!!");
		return false;
	}
	else if (!rcReceived)
		return false;
	else if (ros::Time::now().toSec() - lastRCmsg.toSec() > 1.1)
	{
		// PANIC mode ON
		panicMode = true;

		// Release control
		if (autopilotOn) {
			if (requestControl(0))
				autopilotOn = false;
		}

		alarm->startFlash();
		ROS_WARN("SDK lost connection to RC: PANIC!!!");

		return false;
	}
	else
		return true;
}

////////////////////////////////////////////////////////////
////////////////////////  Main  ////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
	ros::init(argc, argv, "dji_pilot_node");
	ros::NodeHandle nh;

	dji_pilot joydji_pilot(nh);

	ros::Rate rate(25);

	while (ros::ok())
	{
		ros::spinOnce();

		// Check if RC is communicating with the SDK
		if (joydji_pilot.checkRCconnection())
			joydji_pilot.publishCommand();

		rate.sleep();
	}

	return 0;
}
