//----------------------------------------------------------------------------------
//
// Human Robot Interaction Planning Framework
//
// Created on   : 3/7/2017
// Last revision: 3/8/2017
// Author       : Che, Yuhang <yuhangc@stanford.edu>
// Contact      : Che, Yuhang <yuhangc@stanford.edu>
//
//----------------------------------------------------------------------------------

#ifndef HRI_PLANNER_COSTS_H
#define HRI_PLANNER_COSTS_H

#include <vector>
#include <memory>
#include <Eigen/Dense>

#include "hri_planner/cost_feature_bases.h"

namespace hri_planner {

//! a linear cost/reward function class
class LinearCost: public FeatureBase {
public:
    // default constructor
    LinearCost() {
        nfeatures_ = 0;
    }

    // constructor with weights and features
    LinearCost(const std::vector<double>& weights, std::vector<std::shared_ptr<FeatureBase> >& features):
            weights_(weights), features_(features)
    {
        nfeatures_ = (int) weights.size();
    }

    // virtual destructor
    virtual ~LinearCost(){};

    double compute(const Trajectory& robot_traj, const Trajectory& human_traj);

    void grad_uh(const Trajectory& robot_traj, const Trajectory& human_traj, VecRef grad);
    void grad_ur(const Trajectory& robot_traj, const Trajectory& human_traj, VecRef grad);

    // incrementally add in features
    void add_feature(double weight, FeatureBase* feature);
    void add_feature(double weight, const std::shared_ptr<FeatureBase> feature);

protected:
    int nfeatures_;
    std::vector<double> weights_;
    std::vector<std::shared_ptr<FeatureBase> > features_;
};

//! human cost class - extend linear cost to calculate hessians
class HumanCost: public LinearCost {
public:
    void hessian_uh(const Trajectory& robot_traj, const Trajectory& human_traj, MatRef hess);
    void hessian_uh_ur(const Trajectory& robot_traj, const Trajectory& human_traj, MatRef hess);
};

//! cost defined over a single trajectory
class SingleTrajectoryCost: public LinearCost {
public:
    // virtual destructor
    virtual ~SingleTrajectoryCost(){};

    // overloading the () operator and compute function
    virtual double operator()(const Trajectory& traj) {
        return compute(traj);
    }
    virtual double compute(const Trajectory& traj) = 0;

    // a new method that computes gradient
    virtual void grad(const Trajectory& traj, VecRef grad) = 0;

    // set the value for the constant trajectory
    virtual void set_trajectory_data(const Trajectory& traj);

protected:
    Trajectory const_traj_;
};

//! cost defined over the robot trajectory
class SingleTrajectoryCostRobot: public SingleTrajectoryCost {
public:
    using LinearCost::compute;
    // overloading the compute function
    virtual double compute(const Trajectory& traj);
    virtual void grad(const Trajectory& traj, VecRef grad);
};

//! cost defined over the human trajectory
class SingleTrajectoryCostHuman: public SingleTrajectoryCost {
public:
    using LinearCost::compute;
    // overloading the compute function
    virtual double compute(const Trajectory& traj);
    virtual void grad(const Trajectory& traj, VecRef grad);
};

} // namespace

#endif //HRI_PLANNER_COSTS_H
