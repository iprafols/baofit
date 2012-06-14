// Created 28-May-2012 by David Kirkby (University of California, Irvine) <dkirkby@uci.edu>

#include "baofit/CorrelationFitter.h"
#include "baofit/RuntimeError.h"
#include "baofit/AbsCorrelationModel.h"

#include "likely/AbsEngine.h"

namespace local = baofit;

local::CorrelationFitter::CorrelationFitter(AbsCorrelationDataCPtr data, AbsCorrelationModelPtr model)
: _data(data), _model(model), _errorScale(1), _type(data->getTransverseBinningType())
{
    if(!data || 0 == data->getNBinsWithData()) {
        throw RuntimeError("CorrelationFitter: need some data to fit.");
    }
    if(!model) {
        throw RuntimeError("CorrelationFitter: need a model to fit.");
    }
}

local::CorrelationFitter::~CorrelationFitter() { }

void local::CorrelationFitter::setErrorScale(double scale) {
    if(scale <= 0) {
        throw RuntimeError("CorrelationFitter::setErrorScale: expected scale > 0.");
    }
    _errorScale = scale;
}

double local::CorrelationFitter::operator()(likely::Parameters const &params) const {
    // Check that we have the expected number of parameters.
    if(params.size() != _model->getNParameters()) {
        throw RuntimeError("CorrelationFitter: got unexpected number of parameters.");
    }
    // Loop over the dataset bins.
    std::vector<double> pred;
    pred.reserve(_data->getNBinsWithData());
    static int offset(0);
    for(baofit::AbsCorrelationData::IndexIterator iter = _data->begin(); iter != _data->end(); ++iter) {
        int index(*iter);
        double z = _data->getRedshift(index);
        double r = _data->getRadius(index);
        double predicted;
        if(_type == AbsCorrelationData::Coordinate) {
            double mu = _data->getCosAngle(index);
            predicted = _model->evaluate(r,mu,z,params);
        }
        else {
            cosmo::Multipole multipole = _data->getMultipole(index);
            predicted = _model->evaluate(r,multipole,z,params);
        }
        pred.push_back(predicted);
    }
    // Scale chiSquare by 0.5 since the likely minimizer expects a -log(likelihood).
    // Add any model prior on the parameters. The additional factor of _errorScale
    // is to allow arbitrary error contours to be calculated a la MNCONTOUR.
    return (0.5*_data->chiSquare(pred) + _model->evaluatePrior(params))/_errorScale;
}

likely::FunctionMinimumPtr local::CorrelationFitter::fit(std::string const &methodName,
std::string const &config) const {
    likely::FunctionPtr fptr(new likely::Function(*this));
    return _model->findMinimum(fptr,methodName,config);
}
