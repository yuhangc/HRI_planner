//----------------------------------------------------------------------------------
//
// Human Robot Interaction Planning Framework
//
// Created on   : 3/7/2018
// Last revision: 3/22/2018
// Author       : Che, Yuhang <yuhangc@stanford.edu>
// Contact      : Che, Yuhang <yuhangc@stanford.edu>
//
//----------------------------------------------------------------------------------

#include <iostream>
#include <thread>

#include "hri_planner/costs.h"

namespace hri_planner {

//----------------------------------------------------------------------------------
double LinearCost::compute(const Trajectory &robot_traj, const Trajectory &human_traj)
{
    double cost = 0.0;

//    std::vector<double> costs(nfeatures_, 0.0);
//    std::vector<std::thread> th_list;
//
//    for (int i = 0; i < nfeatures_; ++i) {
//        th_list.push_back(std::thread(&FeatureBase::compute_nr, features_[i].get(),
//                                      std::ref(robot_traj), std::ref(human_traj), std::ref(costs[i])));
//    }
//
//    std::for_each(th_list.begin(), th_list.end(), std::mem_fn(&std::thread::join));
//
//    for (int i = 0; i < nfeatures_; ++i) {
//        cost += weights_[i] * costs[i];
//    }

    for (int i = 0; i < nfeatures_; ++i) {
        cost += weights_[i] * features_[i]->compute(robot_traj, human_traj);
    }

    return cost;
}

//----------------------------------------------------------------------------------
void LinearCost::grad_ur(const Trajectory &robot_traj, const Trajectory &human_traj, VecRef grad)
{
    grad.setZero();

    int len = human_traj.traj_control_size();

//    std::vector<std::thread> th_list;
//    std::vector<Eigen::VectorXd> grads(nfeatures_, Eigen::VectorXd());
//
//    for (int i = 0; i < nfeatures_; ++i) {
//        grads[i].resize(len);
//        th_list.push_back(std::thread(&FeatureBase::grad_ur, features_[i].get(),
//                                      std::ref(robot_traj), std::ref(human_traj), VecRef(grads[i])));
//    }
//
//    std::for_each(th_list.begin(), th_list.end(), std::mem_fn(&std::thread::join));
//
//    for (int i = 0; i < nfeatures_; ++i) {
//        grad += weights_[i] * grads[i];
//    }

    for (int i = 0; i < nfeatures_; ++i) {
        Eigen::VectorXd grad_f(len);
        features_[i]->grad_ur(robot_traj, human_traj, grad_f);

        grad += weights_[i] * grad_f;
    }
}

//----------------------------------------------------------------------------------
void LinearCost::grad_uh(const Trajectory &robot_traj, const Trajectory &human_traj, VecRef grad)
{
    grad.setZero();

    int len = human_traj.traj_control_size();

//    std::vector<std::thread> th_list;
//    std::vector<Eigen::VectorXd> grads(nfeatures_, Eigen::VectorXd());
//
//    for (int i = 0; i < nfeatures_; ++i) {
//        grads[i].resize(len);
//        th_list.push_back(std::thread(&FeatureBase::grad_uh, features_[i].get(),
//                                      std::ref(robot_traj), std::ref(human_traj), VecRef(grads[i])));
//    }
//
//    std::for_each(th_list.begin(), th_list.end(), std::mem_fn(&std::thread::join));
//
//    for (int i = 0; i < nfeatures_; ++i) {
//        grad += weights_[i] * grads[i];
//    }

    for (int i = 0; i < nfeatures_; ++i) {
        Eigen::VectorXd grad_f(len);
        features_[i]->grad_uh(robot_traj, human_traj, grad_f);

        grad += weights_[i] * grad_f;
    }
}

//----------------------------------------------------------------------------------
void LinearCost::add_feature(double weight, FeatureBase *feature)
{
    weights_.push_back(weight);
    features_.push_back(std::shared_ptr<FeatureBase>(feature));
}

//----------------------------------------------------------------------------------
void LinearCost::add_feature(double weight, const std::shared_ptr<FeatureBase> feature)
{
    weights_.push_back(weight);
    features_.push_back(feature);
}

//----------------------------------------------------------------------------------
void HumanCost::hessian_uh(const Trajectory &robot_traj, const Trajectory &human_traj, MatRef hess)
{
    hess.setZero();

    int len = human_traj.traj_control_size();
    Eigen::MatrixXd hess_f(len, len);

    for (int i = 0; i < nfeatures_; ++i) {
        static_cast<FeatureHumanCost*>(features_[i].get())->hessian_uh(robot_traj, human_traj, hess_f);

        hess += weights_[i] * hess_f;
    }
}

//----------------------------------------------------------------------------------
void HumanCost::hessian_uh_ur(const Trajectory &robot_traj, const Trajectory &human_traj, MatRef hess)
{
    hess.setZero();

    int len = human_traj.traj_control_size();
    Eigen::MatrixXd hess_f(len, len);

    for (int i = 0; i < nfeatures_; ++i) {
        static_cast<FeatureHumanCost*>(features_[i].get())->hessian_uh_ur(robot_traj, human_traj, hess_f);

        hess += weights_[i] * hess_f;
    }
}

//----------------------------------------------------------------------------------
void SingleTrajectoryCost::set_trajectory_data(const Trajectory &traj)
{
    const_traj_ = traj;
}

//----------------------------------------------------------------------------------
double SingleTrajectoryCostRobot::compute(const Trajectory &traj)
{
    return compute(traj, const_traj_);
}

//----------------------------------------------------------------------------------
void SingleTrajectoryCostRobot::grad(const Trajectory &traj, VecRef grad)
{
    grad_ur(traj, const_traj_, grad);
}

//----------------------------------------------------------------------------------
double SingleTrajectoryCostHuman::compute(const Trajectory &traj)
{
    return compute(const_traj_, traj);
}

//----------------------------------------------------------------------------------
void SingleTrajectoryCostHuman::grad(const Trajectory &traj, VecRef grad)
{
    grad_uh(const_traj_, traj, grad);
}

//----------------------------------------------------------------------------------
void SingleTrajectoryCostHuman::hessian_uh(const Trajectory &robot_traj, const Trajectory &human_traj, MatRef hess)
{
    hess.setZero();

    int len = human_traj.traj_control_size();
    Eigen::MatrixXd hess_f(len, len);

    for (int i = 0; i < nfeatures_; ++i) {
        static_cast<FeatureHumanCost*>(features_[i].get())->hessian_uh(robot_traj, human_traj, hess_f);

        hess += weights_[i] * hess_f;
    }
}

//----------------------------------------------------------------------------------
void SingleTrajectoryCostHuman::hessian_uh_ur(const Trajectory &robot_traj, const Trajectory &human_traj, MatRef hess)
{
    hess.setZero();

    int len = human_traj.traj_control_size();
    Eigen::MatrixXd hess_f(len, len);

    for (int i = 0; i < nfeatures_; ++i) {
        static_cast<FeatureHumanCost*>(features_[i].get())->hessian_uh_ur(robot_traj, human_traj, hess_f);

        hess += weights_[i] * hess_f;
    }
}

} // namespace