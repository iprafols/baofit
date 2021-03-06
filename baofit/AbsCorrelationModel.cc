// Created 07-Apr-2012 by David Kirkby (University of California, Irvine) <dkirkby@uci.edu>

#include "baofit/AbsCorrelationModel.h"
#include "baofit/RuntimeError.h"

#include "cosmo/TransferFunctionPowerSpectrum.h" // for getMultipole(...)

#include "boost/bind.hpp"

#include <cmath>

namespace local = baofit;

local::AbsCorrelationModel::AbsCorrelationModel(std::string const &name)
: FitModel(name), _indexBase(-1), _crossCorrelation(false), _dvIndex(-1)
{ }

local::AbsCorrelationModel::~AbsCorrelationModel() { }

double local::AbsCorrelationModel::evaluate(double r, double mu, double z,
likely::Parameters const &params) {
    bool anyChanged = updateParameterValues(params);
    if(_dvIndex >= 0) _applyVelocityShift(r,mu,z);
    double result = _evaluate(r,mu,z,anyChanged);
    resetParameterValuesChanged();
    return result;
}

double local::AbsCorrelationModel::evaluate(double r, cosmo::Multipole multipole, double z,
likely::Parameters const &params) {
    bool anyChanged = updateParameterValues(params);
    double result = _evaluate(r,multipole,z,anyChanged);
    resetParameterValuesChanged();
    return result;
}

double local::AbsCorrelationModel::_evaluate(double r, cosmo::Multipole multipole, double z,
bool anyChanged) const {
    // Get a pointer to our (r,mu,z) evaluator. We need a typedef here to disambiguate the two
    // overloaded _evaluate methods.
    typedef double (AbsCorrelationModel::*fOfRMuZ)(double, double, double, bool) const;
    fOfRMuZ fptr(&AbsCorrelationModel::_evaluate);
    // Call our (r,mu,z) evaluator once with mu=0 and the input value of anyChanged so it can
    // do any necessary one-time calculations. Subsequent calls will use anyChanged = false.
    (this->*fptr)(r,0,z,anyChanged);
    // Create a smart pointer to a function object of mu with the other args (r,z,anyChanged) bound.
    likely::GenericFunctionPtr fOfMuPtr(
        new likely::GenericFunction(boost::bind(fptr,this,r,_1,z,false)));
    // Finally we have something we can pass to the generic multipole projection integrator.
    return cosmo::getMultipole(fOfMuPtr,(int)multipole);
}

void local::AbsCorrelationModel::_setZRef(double zref) {
    if(zref < 0) throw RuntimeError("AbsCorrelationModel: expected zref >= 0.");
    _zref = zref;
}

int local::AbsCorrelationModel::_defineLinearBiasParameters(double zref, bool crossCorrelation) {
    if(_indexBase >= 0) throw RuntimeError("AbsCorrelationModel: linear bias parameters already defined.");
    _setZRef(zref);
    // Linear bias parameters
    _indexBase = defineParameter("beta",1.4,0.1);
    defineParameter("(1+beta)*bias",-0.336,0.03);
    // Redshift evolution parameters
    defineParameter("gamma-bias",3.8,0.3);
    int last = defineParameter("gamma-beta",0,0.1);    
    if(crossCorrelation) {
        _crossCorrelation = true;
        // Amount to shift each separation's line of sight velocity in km/s
        defineParameter("delta-v",0,10);
        _setDVIndex(_indexBase + DELTA_V);
        // We use don't use beta2 and (1+beta2)*bias2 here since for galaxies or quasars
        // so that this parameter corresponds directly to f = dln(G)/dln(a), which is what
        // we usually want to constrain when the second component is galaxies or quasars.
        defineParameter("bias2",1.4,0.1);
        last = defineParameter("beta2*bias2",-0.336,0.03);
    }
    else {
        // not really necessary since the ctor already does this and you cannot call this method
        // more than once
        _crossCorrelation = false;
    }
    return last;
}

void local::AbsCorrelationModel::_applyVelocityShift(double &r, double &mu, double z) {
    // Lookup value of delta_v
    double dv = getParameterValue(_dvIndex);
    // Convert dv in km/s to dpi in Mpc/h using a flat matter+lambda cosmology with OmegaLambda = 0.73
    double zp1 = 1+z;
    double dpi = (dv/100.)*(1+z)/std::sqrt(0.73+0.27*zp1*zp1*zp1);
    // Calculate the effect of changing pi by dpi in the separation
    double rnew = std::sqrt(r*r + 2*r*mu*dpi + dpi*dpi);
    double munew = (r*mu+dpi)/rnew;
    r = rnew;
    mu = munew;    
}

double local::redshiftEvolution(double p0, double gamma, double z, double zref) {
    return p0*std::pow((1+z)/(1+zref),gamma);
}

double local::AbsCorrelationModel::_getNormFactor(cosmo::Multipole multipole, double z) const {
    if(_indexBase < 0) throw RuntimeError("AbsCorrelationModel: no linear bias parameters defined.");
    // Lookup the linear bias parameters at the reference redshift.
    double beta = getParameterValue(_indexBase + BETA);
    double bb = getParameterValue(_indexBase + BB);
    // Calculate bias from beta and bb.
    double bias = bb/(1+beta);
    // For cross correlations, the linear and quadratic beta terms are independent and
    // the overall bias could be negative.
    double betaAvg,betaProd,biasSq;
    if(_crossCorrelation) {
        double bias2 = getParameterValue(_indexBase + BIAS2);
        double bb2 = getParameterValue(_indexBase + BB2);
        double beta2 = bb2/bias2;
        betaAvg = (beta + beta2)/2;
        betaProd = beta*beta2;
        biasSq = bias*bias2;
    }
    else {
        betaAvg = beta;
        betaProd = beta*beta;
        biasSq = bias*bias;
    }
    // Calculate redshift evolution of biasSq, betaAvg and betaProd.
    double gammaBias = getParameterValue(_indexBase + GAMMA_BIAS);
    double gammaBeta = getParameterValue(_indexBase + GAMMA_BETA);
    biasSq = redshiftEvolution(biasSq,gammaBias,z,_zref);
    betaAvg = redshiftEvolution(betaAvg,gammaBeta,z,_zref);
    betaProd = redshiftEvolution(betaProd,2*gammaBeta,z,_zref);
    // Return the requested normalization factor.
    switch(multipole) {
    case cosmo::Hexadecapole:
        return biasSq*betaProd*(8./35.);
    case cosmo::Quadrupole:
        return biasSq*((4./3.)*betaAvg + (4./7.)*betaProd);
    default:
        return biasSq*(1 + (2./3.)*betaAvg + (1./5.)*betaProd);
    }
}

void  local::AbsCorrelationModel::printToStream(std::ostream &out, std::string const &formatSpec) const {
    FitModel::printToStream(out,formatSpec);
    out << std::endl << "Reference redshift = " << _zref << std::endl;
}
