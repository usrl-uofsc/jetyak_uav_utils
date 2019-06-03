from LQR import LQR
from GPS_utils import GPS_utils

import numpy as np
import threading
from time import sleep

import rospy as rp

from tf.transformations import *
from sensor_msgs.msg import NavSatFix,Joy,Imu
from geometry_msgs.msg import QuaternionStamped,Vector3Stamped
from jetyak_uav_utils.msg import ObservedState
import math

class StatePub():
	def __init__(self,):
		self.firstAtt=True
		self.firstGPS=True
		self.GPSU = GPS_utils()

		rp.init_node("lqr_test")
		self.gpsSub=rp.Subscriber("/dji_sdk/gps_position",NavSatFix,self.gpsCB)
		self.attSub=rp.Subscriber("/dji_sdk/attitude",QuaternionStamped,self.attCB)
		self.velSub=rp.Subscriber("/dji_sdk/velocity",Vector3Stamped,self.velCB)
		self.imuSub=rp.Subscriber("/dji_sdk/imu",Imu,self.imuCB)
		self.statePub=rp.Publisher("/jetyak_uav_vision/state",ObservedState,queue_size=1)
		self.state = ObservedState()
		rp.spin()
	def publish(self,):
		self.state.header.stamp = rp.Time.now()
		self.statePub.publish(self.state)
	def attCB(self,msg):
		if(self.firstAtt):
			self.firstAtt=False
		(r,p,y) =euler_from_quaternion([msg.quaternion.x,msg.quaternion.y,msg.quaternion.z,msg.quaternion.w])
		self.state.drone_q.x=r
		self.state.drone_q.y=p
		self.state.drone_q.z=y
		self.yaw=y
		self.publish()

	def imuCB(self,msg):
		self.state.drone_qdot.x=msg.angular_velocity.x
		self.state.drone_qdot.y=msg.angular_velocity.y
		self.state.drone_qdot.z=msg.angular_velocity.z
		self.publish()

	def velCB(self,msg):
		if(not self.firstAtt):
			x=msg.vector.x
			y=msg.vector.y
			self.state.drone_pdot.x=msg.vector.x
			self.state.drone_pdot.y=msg.vector.y
			self.state.drone_pdot.z=msg.vector.z
			self.publish()
	
	def gpsCB(self,msg):
		if(self.firstGPS):
			self.GPSU.setENUorigin(0,0,0)
		p=self.GPSU.geo2enu(msg.latitude,msg.longitude,msg.altitude)
		self.state.drone_p.x=p[0]
		self.state.drone_p.y=p[1]
		self.state.drone_p.z=p[2]
		self.firstGPS=False
		self.publish()

lll = StatePub()