//----------------------------------------------------------------------------------
//
// Human Robot Interaction Planning Framework
//
// Created on   : 3/18/2017
// Last revision: 3/25/2017
// Author       : Che, Yuhang <yuhangc@stanford.edu>
// Contact      : Che, Yuhang <yuhangc@stanford.edu>
//
//----------------------------------------------------------------------------------

#include "hri_planner/cost_features_vectorized.h"

namespace hri_planner {

//----------------------------------------------------------------------------------
std::shared_ptr<FeatureVectorizedBase> FeatureVectorizedBase::create(const std::string &feature_type,
                                                                     const std::vector<double> &args)
{
    if (feature_type == "HumanEffort") {
        return std::make_shared<HumanAccCostVec>();
    }
    else if (feature_type == "Collision") {
        return std::make_shared<CollisionCostVec>(args[0]);
    }
    else if (feature_type == "HumanGoal") {
        Eigen::VectorXd x_goal(2);
        x_goal << args[0], args[1];
        return std::make_shared<HumanGoalCostVec>(x_goal);
    }
    else {
        throw "Invalid feature type!";
    }
}

//----------------------------------------------------------------------------------
void GaussianCostVec::compute(const Eigen::VectorXd &x, const int nX, const int T, const double a, const double b,
                              Eigen::VectorXd &costs)
{
    costs.setZero(T);

    for (int t = 0; t < T; ++t) {
        double xt = x(t*2) / a;
        double yt = x(t*2+1) / b;
        costs(t) = std::exp(-(xt * xt + yt * yt));
    }
}

//----------------------------------------------------------------------------------
void GaussianCostVec::grad(const Eigen::VectorXd &x, const int nX, const int T, const double a, const double b,
                           Eigen::VectorXd &grad)
{
    grad.setZero(T * nX);

    for (int t = 0; t < T; ++t) {
        int st = t * nX;
        double xt = x(t*2) / a;
        double yt = x(t*2+1) / b;
        double c = std::exp(-(xt * xt + yt * yt));

        grad(st) = -2.0 * xt * c / a;
        grad(st+1) = -2.0 * yt * c / b;
    }
}

//----------------------------------------------------------------------------------
void CollisionCostVec::compute(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::VectorXd &costs)
{
    // construct the pos diff vector
    Eigen::VectorXd x_diff(2 * robot_traj.horizon());

    int nXr = robot_traj.state_size();
    int nXh = human_traj.state_size();

    for (int t = 0; t < robot_traj.horizon(); ++t) {
        int str = t * nXr;
        int sth = t * nXh;
        x_diff(t*2) = robot_traj.x(str) - human_traj.x(sth);
        x_diff(t*2+1) = robot_traj.x(str+1) - human_traj.x(sth+1);
    }

    GaussianCostVec::compute(x_diff, 2, robot_traj.horizon(), R_, R_, costs);
}

//----------------------------------------------------------------------------------
void CollisionCostVec::grad_uh(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::MatrixXd &Juh)
{
    // construct the pos diff vector
    Eigen::VectorXd x_diff(2 * human_traj.horizon());

    int nXr = robot_traj.state_size();
    int nXh = human_traj.state_size();

    for (int t = 0; t < human_traj.horizon(); ++t) {
        int str = t * nXr;
        int sth = t * nXh;
        x_diff(t*2) = human_traj.x(sth) - robot_traj.x(str);
        x_diff(t*2+1) = human_traj.x(sth+1) - robot_traj.x(str+1);
    }

    // compute gradient
    Eigen::VectorXd grad_x;
    GaussianCostVec::grad(x_diff, 2, human_traj.horizon(), R_, R_, grad_x);

    Juh.setZero(human_traj.horizon(), human_traj.traj_control_size());
    for (int t = 0; t < human_traj.horizon(); ++t) {
        Juh.row(t) = grad_x.segment(t*2, 2).transpose() *
                human_traj.Ju.block(t*nXh, 0, 2, human_traj.traj_control_size());
    }
}

//----------------------------------------------------------------------------------
void CollisionCostVec::grad_ur(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::MatrixXd &Jur)
{
    // construct the pos diff vector
    Eigen::VectorXd x_diff(2 * human_traj.horizon());

    int nXr = robot_traj.state_size();
    int nXh = human_traj.state_size();

    for (int t = 0; t < human_traj.horizon(); ++t) {
        int str = t * nXr;
        int sth = t * nXh;
        x_diff(t*2) = robot_traj.x(str) - human_traj.x(sth);
        x_diff(t*2+1) = robot_traj.x(str+1) - human_traj.x(sth+1);
    }

    // compute gradient
    Eigen::VectorXd grad_x;
    GaussianCostVec::grad(x_diff, 2, robot_traj.horizon(), R_, R_, grad_x);

    Jur.setZero(robot_traj.horizon(), robot_traj.traj_control_size());
    for (int t = 0; t < human_traj.horizon(); ++t) {
        Jur.row(t) = grad_x.segment(t*2, 2).transpose() *
                     robot_traj.Ju.block(t*nXr, 0, 2, robot_traj.traj_control_size());
    }
}

//----------------------------------------------------------------------------------
void HumanAccCostVec::compute(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::VectorXd &costs)
{
    costs.setZero(human_traj.horizon());

    int nUh = human_traj.control_size();
    for (int t = 0; t < human_traj.horizon(); ++t) {
        costs(t) = human_traj.u.segment(t * nUh, 2).squaredNorm();
    }
}

//----------------------------------------------------------------------------------
void HumanAccCostVec::grad_uh(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::MatrixXd &Juh)
{
    // simply "diagonal"
    Juh.setZero(human_traj.horizon(), human_traj.traj_control_size());

    int nUh = human_traj.control_size();
    for (int t = 0; t < human_traj.horizon(); ++t) {
        int stu = t * nUh;
        Juh(t, stu) = 2.0 * human_traj.u(stu);
        Juh(t, stu+1) = 2.0 * human_traj.u(stu+1);
    }
}

//----------------------------------------------------------------------------------
void HumanAccCostVec::grad_ur(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::MatrixXd &Jur)
{
    // doesn't depend on ur
    Jur.setZero(robot_traj.horizon(), robot_traj.traj_control_size());
}

//----------------------------------------------------------------------------------
void HumanGoalCostVec::compute(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::VectorXd &costs)
{
    int xs = human_traj.traj_state_size() - human_traj.state_size();
    double x_diff = x_goal_(0) - human_traj.x(xs);
    double y_diff = x_goal_(1) - human_traj.x(xs+1);

    costs.setZero(human_traj.horizon());
    costs(human_traj.horizon()-1) = std::sqrt(x_diff * x_diff + y_diff * y_diff);
}

//----------------------------------------------------------------------------------
void HumanGoalCostVec::grad_uh(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::MatrixXd &Juh)
{
    Eigen::Vector2d grad_x;
    int xs = human_traj.traj_state_size() - human_traj.state_size();
    double x_diff = human_traj.x(xs) - x_goal_(0);
    double y_diff = human_traj.x(xs+1) - x_goal_(1);
    double d = std::sqrt(x_diff * x_diff + y_diff * y_diff) + reg_;

    grad_x(0) = x_diff / d;
    grad_x(1) = y_diff / d;

    // only the last row has non-zero elements
    Juh.setZero(human_traj.horizon(), human_traj.traj_control_size());
    Juh.row(human_traj.horizon()-1) = grad_x.transpose() * human_traj.Ju.middleRows(xs, 2);
}

//----------------------------------------------------------------------------------
void HumanGoalCostVec::grad_ur(const Trajectory &robot_traj, const Trajectory &human_traj, Eigen::MatrixXd &Jur)
{
    // doesn't depend on ur
    Jur.setZero(robot_traj.horizon(), robot_traj.traj_control_size());
}

}