"""
MIT License

Copyright(c) 2018 Brennan Cain and Michail Kalaitzakis(Unmanned Systems and Robotics Lab, University of South Carolina, USA)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
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

Author: Michail Kalaitzakis

"""
import numpy as np

class KalmanFilter:
	"""
		Kalman Filter

		n  -> Number of States
		X  -> State
		F  -> State Transition Matrix Discrete
		P  -> Process Matrix
		Q  -> Process Noise Matrix
		N  -> Noise Level
		Cf -> Continuous White Noise Matrix
	"""

	def __init__(self):
		self.n  = None
		self.X  = None
		self.F  = None
		self.P  = None
		self.Q  = None
		self.N  = None
		self.Cf = None

	def initialize(self, X, F, P, N):
		self.X = X
		self.F = F
		self.P = P
		self.N = N
		self.n = np.shape(F)[0]

		diagCf = np.array([1e-1, 1e-1, 1e1,
		 				   1e0, 1e0, 1e0,
		 				   1e0, 1e0, 1e0, 1e0,
		 				   1e0, 1e0, 1e0, 1e0,
		 				   1e0, 1e0, 1e-3,
		 				   1e0, 1e0,
		 				   1e-3,
		 				   1e0, 1e0, 1e0,
		 				   1e-3])
		self.Cf = np.diag(diagCf)

	def updateQ(self):
		self.Q = self.F * self.Cf * self.F.T * self.N
	
	def updateF(self, dt):
		self.F[  0:3,   3:6] = np.matrix(dt * np.eye(3))
		#self.F[ 6:10, 10:14] = np.matrix(dt * np.eye(4))
		self.F[14:16, 17:19] = np.matrix(dt * np.eye(2))

	def predict(self):
		self.X = self.F * self.X
		self.checkQuaternion()
		self.P = self.F * self.P * self.F.T + self.Q
		self.P = (self.P + self.P.T) / 2

	def correct(self, z, H, R):
		hatP = self.P * H.T
		S    = H * hatP + R
		K    = hatP * S.I
		I    = np.asmatrix(np.eye(self.n))

		self.X = self.X + K * (z - H * self.X)
		self.checkQuaternion()
		self.P = (I - K * H) * self.P
		self.P = (self.P + self.P.T) / 2
	
	def checkQuaternion(self):
		qx = self.X.item(6)
		qy = self.X.item(7)
		qz = self.X.item(8)
		qw = self.X.item(9)

		mag = pow(pow(qx,2) + pow(qy,2) + pow(qz,2) + pow(qw,2), 0.5)

		if mag > 0:
			self.X.item(6) / mag
			self.X.item(7) / mag
			self.X.item(8) / mag
			self.X.item(9) / mag

	def getState(self):
		return self.X
