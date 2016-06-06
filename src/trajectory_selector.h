#include <Eigen/Dense>
#include <iostream>
#include <math.h>
#include <opencv2/opencv.hpp>
#include "trajectory_library.h"
#include "trajectory_evaluator.h"


class TrajectorySelector {
public:
  void Test();
  
  void InitializeLibrary(double const& final_time);
  void setInitialVelocity(Vector3 const& initialVelocity);
  size_t getNumTrajectories();
  
  Vector3 getSigmaAtTime(double const& t);
  Vector3 getInverseSigmaAtTime(double const & t);
  size_t computeBestTrajectoryIndex(Eigen::Matrix<Scalar, 100, 3> const& point_cloud_xyz_samples, Vector3 const& carrot_body_frame);

  Eigen::Matrix<Scalar, Eigen::Dynamic, 3> sampleTrajectoryForDrawing(size_t trajectory_index, Eigen::Matrix<Scalar, Eigen::Dynamic, 1> sampling_time_vector, size_t num_samples);

private:
  
  TrajectoryLibrary trajectory_library;
  size_t num_trajectories = 25;
  TrajectoryEvaluator trajectory_evaluator;

  void EvaluateGoalProgress(Vector3 const& carrot_body_frame);
  void EvaluateCollisionProbabilities(Eigen::Matrix<Scalar, 100, 3> const& point_cloud_xyz_samples);
  double computeProbabilityOfCollisionOneTrajectory(Trajectory trajectory, Eigen::Matrix<Scalar, 100, 3> const& point_cloud_xyz_samples);
  double computeProbabilityOfCollisionOneStepOneObstacle(Vector3 const& trajectory_position, Vector3 const& point, Vector3 const& inverse_sigma_at_time);

  double final_time;
  double start_time = 0.0;

  Eigen::Matrix<Scalar, 10, 1> sampling_time_vector;

  Eigen::Matrix<Scalar, 25, 1> GoalProgressEvaluations;
  Eigen::Matrix<Scalar, 25, 1> CollisionProbabilities;

};