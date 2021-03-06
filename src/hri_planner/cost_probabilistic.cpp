//----------------------------------------------------------------------------------
//
// Human Robot Interaction Planning Framework
//
// Created on   : 3/18/2018
// Last revision: 3/28/2018
// Author       : Che, Yuhang <yuhangc@stanford.edu>
// Contact      : Che, Yuhang <yuhangc@stanford.edu>
//
//----------------------------------------------------------------------------------

#include "hri_planner/cost_probabilistic.h"

namespace hri_planner {

//----------------------------------------------------------------------------------
void ProbabilisticCostBase::set_features_non_int(const std::vector<double> &w,
                                                 const std::vector<std::shared_ptr<FeatureBase> > &f)
{
    w_non_int_ = w;
    f_non_int_ = f;
}

//----------------------------------------------------------------------------------
void ProbabilisticCostBase::set_features_int(const std::vector<double> &w,
                                             const std::vector<std::shared_ptr<FeatureVectorizedBase> > &f)
{
    w_int_ = w;
    f_int_ = f;
}

//----------------------------------------------------------------------------------
double ProbabilisticCost::compute(const Trajectory& robot_traj, const Trajectory& human_traj_hp,
                                  const Trajectory& human_traj_rp, int acomm, double tcomm,
                                  Eigen::VectorXd& grad_ur, Eigen::VectorXd& grad_hp, Eigen::VectorXd& grad_rp)
{
    double cost = 0.0;

    //! first compute non-interactive costs
    // doesn't matter which human trajectory to use
    for (int i = 0; i < w_non_int_.size(); ++i)
        cost += w_non_int_[i] * f_non_int_[i]->compute(robot_traj, human_traj_hp);

    //! compute the interactive costs weighted by beliefs
    Eigen::VectorXd costs_hp;
    Eigen::VectorXd costs_rp;

    // compute the vectorized costs first
    int T = robot_traj.horizon();
    costs_hp.setZero(T);
    costs_rp.setZero(T);

    Eigen::VectorXd cost_vec;
    for (int i = 0; i < w_int_.size(); ++i) {
        f_int_[i]->compute(robot_traj, human_traj_hp, cost_vec);
        costs_hp += w_int_[i] * cost_vec;

        f_int_[i]->compute(robot_traj, human_traj_rp, cost_vec);
        costs_rp += w_int_[i] * cost_vec;
    }

    // create a human trajectory with const vel prediction
    // Trajectory human_traj_const(CONST_ACC_MODEL, human_traj_hp.horizon(), human_traj_hp.dt());

    // FIXME: assuming that "current time" is always 0, and tcomm is adjusted already
    Eigen::VectorXd prob_hp(T);
    Eigen::MatrixXd Jur(T, robot_traj.traj_control_size());
    belief_model_->update_belief(robot_traj, human_traj_pred_, acomm, tcomm, 0.0, prob_hp, Jur);

    Eigen::VectorXd prob_rp;
    prob_rp.setOnes(T);
    prob_rp -= prob_hp;

    cost_hp_ = prob_hp.dot(costs_hp);
    cost_rp_ = prob_rp.dot(costs_rp);
    cost += cost_hp_ + cost_rp_;

    // output for debug
    static int counter = 0;
    ++counter;
//
//    std::cout << "control at iteration " << counter << " are: " << std::endl;
//    std::cout << human_traj_hp.u.transpose() << std::endl;
//    std::cout << human_traj_rp.u.transpose() << std::endl;
//    std::cout << "---------------" << std::endl;
//
    std::cout << "cost at iteration " << counter << " is: " << cost << ", cost vectors are:" << std::endl;
    std::cout << costs_hp.transpose() << std::endl;
    std::cout << costs_rp.transpose() << std::endl;
    std::cout << "-------------------" << std::endl;
    std::cout << "probabilities are:" << std::endl;
    std::cout << prob_hp.transpose() << std::endl;
    std::cout << "------------------" << std::endl;
    std::cout << "controls are:" << std::endl;
    std::cout << robot_traj.u.transpose() << std::endl;
    std::cout << std::endl;

    //! compute the gradient w.r.t. ur
    // non-interactive features
    grad_ur.setZero(robot_traj.traj_control_size());
    Eigen::VectorXd grad(robot_traj.traj_control_size());
    for (int i = 0; i < w_non_int_.size(); ++i) {
        f_non_int_[i]->grad_ur(robot_traj, human_traj_hp, grad);
        grad_ur += w_non_int_[i] * grad;
    }

    // compute the jacobians for the interactive cost vectors
    Eigen::MatrixXd Jc_hp;
    Eigen::MatrixXd Jc_rp;
    Eigen::MatrixXd Jc;

    Jc_hp.setZero(T, robot_traj.traj_control_size());
    Jc_rp.setZero(T, robot_traj.traj_control_size());

    for (int i = 0; i < w_int_.size(); ++i) {
        f_int_[i]->grad_ur(robot_traj, human_traj_hp, Jc);
        Jc_hp += w_int_[i] * Jc;

        f_int_[i]->grad_ur(robot_traj, human_traj_rp, Jc);
        Jc_rp += w_int_[i] * Jc;
    }

    grad_ur += Jur.transpose() * (costs_hp - costs_rp) + Jc_hp.transpose() * prob_hp + Jc_rp.transpose() * prob_rp;

    //! compute gradient w.r.t. uh_hp and uh_rp
    Jc_hp.setZero(T, robot_traj.traj_control_size());
    Jc_rp.setZero(T, robot_traj.traj_control_size());

    for (int i = 0; i < w_int_.size(); ++i) {
        f_int_[i]->grad_uh(robot_traj, human_traj_hp, Jc);
        Jc_hp += w_int_[i] * Jc;

        f_int_[i]->grad_uh(robot_traj, human_traj_rp, Jc);
        Jc_rp += w_int_[i] * Jc;
    }

    grad_hp = Jc_hp.transpose() * prob_hp;
    grad_rp = Jc_rp.transpose() * prob_rp;

    return cost;
}

//----------------------------------------------------------------------------------
double ProbabilisticCostSimplified::compute(const Trajectory &robot_traj, const Trajectory &human_traj_hp,
                                            const Trajectory &human_traj_rp, int acomm, double tcomm,
                                            Eigen::VectorXd &grad_ur, Eigen::VectorXd &grad_hp,
                                            Eigen::VectorXd &grad_rp)
{
    double cost = 0.0;

    //! first compute non-interactive costs
    // doesn't matter which human trajectory to use
    costs_non_int_.clear();
    for (int i = 0; i < w_non_int_.size(); ++i) {
        double t_cost = f_non_int_[i]->compute(robot_traj, human_traj_hp);
        costs_non_int_.push_back(t_cost);
        cost += w_non_int_[i] * t_cost;
    }

    //! compute the interactive costs
    Eigen::VectorXd costs_hp;
    Eigen::VectorXd costs_rp;

    // compute the vectorized costs first
    int T = robot_traj.horizon();
    costs_hp.setZero(T);
    costs_rp.setZero(T);

    Eigen::VectorXd cost_vec;
    for (int i = 0; i < w_int_.size(); ++i) {
        f_int_[i]->compute(robot_traj, human_traj_hp, cost_vec);
        costs_hp += w_int_[i] * cost_vec;

        f_int_[i]->compute(robot_traj, human_traj_rp, cost_vec);
        costs_rp += w_int_[i] * cost_vec;
    }

    // update the current belief with explicit communication
    // FIXME: assuming that "current time" is always 0, and tcomm is adjusted already
    double prob_hp = belief_model_->update_belief(acomm, tcomm, 0.0);
    double prob_rp = 1.0 - prob_hp;

    Eigen::VectorXd ones;
    ones.setOnes(costs_hp.size());

    cost_hp_ = ones.dot(costs_hp);
    cost_rp_ = ones.dot(costs_rp);

//    std::cout << ">>>>>>>> costs are: " << cost << ", (";
//
//    for (auto c: costs_non_int_)
//        std::cout << c << ", ";
//
//    std::cout << "), (" << prob_hp << ", " << cost_hp_ << "), ("
//              << prob_rp << ", " << cost_rp_ << "), | ";

    cost += prob_hp * cost_hp_ + prob_rp * cost_rp_;

//    std::cout << cost << std::endl;


    //! compute the gradient w.r.t. ur
    // non-interactive features
    grad_ur.setZero(robot_traj.traj_control_size());
    Eigen::VectorXd grad(robot_traj.traj_control_size());
    for (int i = 0; i < w_non_int_.size(); ++i) {
        f_non_int_[i]->grad_ur(robot_traj, human_traj_hp, grad);
        grad_ur += w_non_int_[i] * grad;
    }

    // compute the jacobians for the interactive cost vectors
    Eigen::MatrixXd Jc_hp;
    Eigen::MatrixXd Jc_rp;
    Eigen::MatrixXd Jc;

    Jc_hp.setZero(T, robot_traj.traj_control_size());
    Jc_rp.setZero(T, robot_traj.traj_control_size());

    for (int i = 0; i < w_int_.size(); ++i) {
        f_int_[i]->grad_ur(robot_traj, human_traj_hp, Jc);
        Jc_hp += w_int_[i] * Jc;

        f_int_[i]->grad_ur(robot_traj, human_traj_rp, Jc);
        Jc_rp += w_int_[i] * Jc;
    }

    Eigen::VectorXd grad_inc = Jc_hp.transpose() * ones * prob_hp + Jc_rp.transpose() * ones * prob_rp;
    grad_ur += grad_inc;
//    std::cout << "---------------" << std::endl;
//    std::cout << grad_inc.transpose() << std::endl;
//    std::cout << "---------------" << std::endl;
//    grad_ur += Jc_hp.transpose() * ones * prob_hp + Jc_rp.transpose() * ones * prob_rp;

    //! compute gradient w.r.t. uh_hp and uh_rp
    Jc_hp.setZero(T, robot_traj.traj_control_size());
    Jc_rp.setZero(T, robot_traj.traj_control_size());

    for (int i = 0; i < w_int_.size(); ++i) {
        f_int_[i]->grad_uh(robot_traj, human_traj_hp, Jc);
        Jc_hp += w_int_[i] * Jc;

        f_int_[i]->grad_uh(robot_traj, human_traj_rp, Jc);
        Jc_rp += w_int_[i] * Jc;
    }

    grad_hp = Jc_hp.transpose() * ones * prob_hp;
    grad_rp = Jc_rp.transpose() * ones * prob_rp;

    return cost;
}

}
