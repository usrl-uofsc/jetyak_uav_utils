#include "jetyak_uav_utils/take_off_follow.h"
#include <iostream>
take_off_follow::take_off_follow(ros::NodeHandle& nh) :
  xpid_(NULL),
  ypid_(NULL),
  zpid_(NULL),
  wpid_(NULL)
{
  takeoffPub_ = nh.advertise<std_msgs::Empty>("takeoff",1);
  cmdPub_ = nh.advertise<geometry_msgs::Twist>("raw_cmd_vel_FLU",1);
  modePub_ = nh.advertise<jetyak_uav_utils::Mode>("uav_mode",1);

  arTagSub_ = nh.subscribe("/ar_pose_marker",1,&take_off_follow::arTagCallback, this);
  modeSub_ = nh.subscribe("uav_mode",1,&take_off_follow::modeCallback,this);
}

void take_off_follow::arTagCallback(const ar_track_alvar_msgs::AlvarMarkers::ConstPtr& msg)
{
  if(!msg->markers.empty()) {

  }
  if(currentMode_==jetyak_uav_utils::Mode::FOLLOWING)
  {
    if(!msg->markers.empty())
    {

      droneLastSeen_=ros::Time::now().toSec();
      tf2::Transform transform_from_camera;
      tf2::fromMsg(msg->markers[0].pose.pose,transform_from_camera);
      geometry_msgs::Pose pose_from_tag;
      const tf2::Transform transform_from_tag = transform_from_camera.inverse();
      tf2::toMsg(transform_from_tag,pose_from_tag);
      const geometry_msgs::Quaternion* orientation = const_cast<const geometry_msgs::Quaternion*>(&pose_from_tag.orientation);
      geometry_msgs::Vector3* state = new geometry_msgs::Vector3();
      bsc_common::util::rpy_from_quat(orientation,state);
      double yaw = state->z+bsc_common::util::C_PI/2;
      if(yaw>bsc_common::util::C_PI) {
        yaw=yaw-2*bsc_common::util::C_PI;
      }

      // Get drone last_cmd_update_

      if(firstFollowLoop_)
      {
        firstFollowLoop_=false;
        xpid_->reset();
        ypid_->reset();
        zpid_->reset();
        wpid_->reset();

        takeoffPub_.publish(std_msgs::Empty());
      }
      else
      {
        xpid_->update(follow_pos_.x-pose_from_tag.position.x);
        ypid_->update(follow_pos_.y-pose_from_tag.position.y);
        zpid_->update(follow_pos_.z-pose_from_tag.position.z);
        wpid_->update(follow_pos_.w-yaw); //yaw of the tag TODO: check sign and axis
        ROS_WARN("x: %.2f, y: %.2f, z: %.2f, yaw: %.2f",pose_from_tag.position.x,pose_from_tag.position.y,pose_from_tag.position.z,yaw);

        //rotate velocities in reference to the tag
        double *rotated_x;
        double *rotated_y;
        bsc_common::util::rotate_vector(
          xpid_->get_signal(),ypid_->get_signal(),-yaw,rotated_x,rotated_y);

        geometry_msgs::Twist cmdT;
        cmdT.linear.x=*rotated_x;
        cmdT.linear.y=*rotated_y;
        cmdT.linear.z=zpid_->get_signal();
        cmdT.angular.x = 0;
        cmdT.angular.y = 0;
        cmdT.angular.z=wpid_->get_signal();
        cmdPub_.publish(cmdT);
      }
      delete state;
    }
    else
    { //if not seen in more than a sec, stop and spin. after 5, search
      if(ros::Time::now().toSec()-droneLastSeen_>5)
      { // if not seen in 5 sec
        jetyak_uav_utils::Mode m;
        m.mode=jetyak_uav_utils::Mode::SEARCHING;
        modePub_.publish(m);
      }
      else if(ros::Time::now().toSec()-droneLastSeen_>1)
      { //if not seen in 1 sec
        geometry_msgs::Twist cmdT;
        cmdT.linear.x = 0;
        cmdT.linear.y = 0;
        cmdT.linear.z = 0;
        cmdT.angular.x = 0;
        cmdT.angular.y = 0;
        // TODO: if the gimbal is used rotate camera not drone
        cmdT.angular.z = 1.5;
        cmdPub_.publish(cmdT);
      }
    }
  } else {
    firstFollowLoop_ = true;
  }
}
void take_off_follow::modeCallback(const jetyak_uav_utils::Mode::ConstPtr& msg)
{
  currentMode_=msg->mode;
  if(currentMode_==jetyak_uav_utils::Mode::FOLLOWING)
  {
    firstFollowLoop_=true;
    droneLastSeen_=0;
  }
  else
  {
    droneLastSeen_=0;
  }
}

void take_off_follow::reconfigureCallback(jetyak_uav_utils::FollowConstantsConfig &config, uint32_t level)
{
  ROS_WARN("%s","Reconfigure received by take_off_follow");
  kp_.x=config.kp_x;
  kp_.y=config.kp_y;
  kp_.z=config.kp_z;
  kp_.w=config.kp_w;

  kd_.x=config.kd_x;
  kd_.y=config.kd_y;
  kd_.z=config.kd_z;
  kd_.w=config.kd_w;

  ki_.x=config.ki_x;
  ki_.y=config.ki_y;
  ki_.z=config.ki_z;
  ki_.w=config.ki_w;

  followPose_.x=config.follow_x;
  followPose_.y=config.follow_y;
  followPose_.z=config.follow_z;
  followPose_.w=config.follow_w;

  if (xpid_ != NULL)
  {
    xpid_->updateParams(kp_.x,ki_.x,kd_.x);
    ypid_->updateParams(kp_.y,ki_.y,kd_.y);
    zpid_->updateParams(kp_.z,ki_.z,kd_.z);
    wpid_->updateParams(kp_.w,ki_.w,kd_.w);
  } else {
    xpid_ = new bsc_common::PID(kp_.x,ki_.x,kd_.x);
    ypid_ = new bsc_common::PID(kp_.y,ki_.y,kd_.y);
    zpid_ = new bsc_common::PID(kp_.z,ki_.z,kd_.z);
    wpid_ = new bsc_common::PID(kp_.w,ki_.w,kd_.w);
  }

}

int main(int argc, char *argv[])
{
  ros::init(argc,argv,"take_off_follow");
  ros::NodeHandle nh;
  take_off_follow takeOffFollow(nh);

  dynamic_reconfigure::Server<jetyak_uav_utils::FollowConstantsConfig> server;
  dynamic_reconfigure::Server<jetyak_uav_utils::FollowConstantsConfig>::CallbackType f;
  boost::function<void (jetyak_uav_utils::FollowConstantsConfig &,int) >
      f2( boost::bind( &take_off_follow::reconfigureCallback,&takeOffFollow, _1, _2 ) );

  f=f2;
  server.setCallback(f);
  ros::spin();
  return 0;
}
