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
 * This node performs necessary transformations between the body, camera, and gimbal frames. 
 * 
 * Author: Michail Kalaitzakis
 */

#ifndef GIMBAL_TAG_H
#define GIMBAL_TAG_H

// ROS includes
#include "ar_track_alvar_msgs/AlvarMarkers.h"
#include "geometry_msgs/Vector3Stamped.h"
#include "ros/ros.h"
#include "tf/tf.h"

// DJI SDK includes
#include <dji_sdk/QueryDroneVersion.h>
#include "dji_sdk/dji_sdk.h"

#define C_PI (double)3.141592653589793
#define DEG2RAD(DEG) ((DEG) * ((C_PI) / (180.0)))
#define RAD2DEG(RAD) ((RAD) * (180.0) / (C_PI))

class gimbal_tag
{
public:
	/** gimbal_tag
	 * Constructs the node using a node handle
	 */
	gimbal_tag(ros::NodeHandle &nh);
	~gimbal_tag(){};

	// Publisher
	/** publishTagPose
	 * If tag found, convert the tag into the UAV body frame and publish.
	 */
	void publishTagPose();

private:
	// Subscribers
	ros::Subscriber tagPoseSub;
	ros::Subscriber gimbalAngleSub;
	ros::Subscriber vehicleAttiSub;

	// Publishers
	ros::Publisher tagBodyPosePub, tagPosePub;

	// Functions
	/** changeTagAxes
	 * Changes the tag axes to patch FLU conventions.
	 *
	 * @param tagBody Quaternion defining the rotation of the tag.
	 */
	void changeTagAxes(tf::Quaternion &tagBody);

	// Callbacks
	/** tagCallback
	 * Saves the tag and rotates it into the gimbal frame.
	 *
	 * @param msg Tag message
	 */
	void tagCallback(const ar_track_alvar_msgs::AlvarMarkers &msg);

	/** gimbalCallback
	 * Saves the orientation of the gimbal.
	 *
	 * @param msg gimbal orientation in RPY
	 */
	void gimbalCallback(const geometry_msgs::Vector3Stamped &msg);

	/** attitudeCallback
	 * creates a transform from quaternion of attitude and saves as qVehicle.
	 */
	void attitudeCallback(const geometry_msgs::QuaternionStamped &msg);

	// Data
	tf::Quaternion qCamera2Gimbal;
	tf::Quaternion qTagFix;
	tf::Quaternion qConstant;
	tf::Quaternion qOffset;
	tf::Quaternion qGimbal;
	tf::Quaternion qVehicle;
	tf::Quaternion qTag;
	tf::Quaternion posTag;

	bool tagFound;
	bool isM100;
};

#endif
