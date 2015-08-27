/**
 * Common.h
 * @author Florian Corzilius
 * @since 2013-10-07
 * @version 2013-10-21
 */

#pragma once

#include <vector>
#include <set>
#include <string.h>
#include <unordered_set>
#include <unordered_map>
#include "logging.h"
#include <carl/core/MultivariatePolynomial.h>
#include <carl/core/FactorizedPolynomial.h>
#include <carl/core/Variable.h>
#include <carl/core/VariablePool.h>
#include <carl/interval/Interval.h>
#include <carl/interval/IntervalEvaluation.h>
#include <carl/interval/Contraction.h>
#include <carl/io/streamingOperators.h>
#include <carl/util/Common.h>
#include <carl/formula/FormulaPool.h>
#include <carl/formula/uninterpreted/UFManager.h>
#include <carl/formula/uninterpreted/UFInstanceManager.h>
#include <carl/formula/bitvector/BVTerm.h>
#include <carl/formula/bitvector/BVTermPool.h>
#include <carl/formula/bitvector/BVConstraintPool.h>
#include <carl/formula/bitvector/BVConstraint.h>

namespace smtrat
{
	using carl::operator<<;

    // Enumerations.
    
    enum class Variable_Domain: unsigned { BOOLEAN = 0, REAL = 1, INTEGER = 2 };
    
    enum class Logic : unsigned { UNDEFINED, QF_NRA, QF_LRA, QF_NIA, QF_LIA, QF_UF, QF_BV };
	inline std::ostream& operator<<(std::ostream& os, const Logic& l) {
	switch (l) {
		case Logic::UNDEFINED:	os << "undefined"; break;
		case Logic::QF_NRA:		os << "QF_NRA"; break;
		case Logic::QF_LRA:		os << "QF_LRA"; break;
		case Logic::QF_NIA:		os << "QF_NIA"; break;
		case Logic::QF_LIA:		os << "QF_LIA"; break;
		case Logic::QF_UF:		os << "QF_UF"; break;
		case Logic::QF_BV:		os << "QF_BV"; break;
	}
	return os;
}
    
    
    ///An enum with the possible answer a Module can give
    enum Answer { True = 0, False = 1, Unknown = 2 };
    
    // Further type definitions.

    typedef mpq_class Rational;
    
	typedef carl::IntegralType<Rational>::type Integer;
    
    typedef carl::Term<Rational> TermT;
    
    typedef carl::MultivariatePolynomial<Rational> Poly;
//    typedef carl::FactorizedPolynomial<carl::MultivariatePolynomial<Rational>> Poly;
    
    typedef carl::Constraint<Poly> ConstraintT;
    
    typedef carl::Constraints<Poly> ConstraintsT;
    
    typedef carl::Formula<Poly> FormulaT;
    
    typedef carl::Formulas<Poly> FormulasT;
	
	typedef carl::FormulaSet<Poly> FormulaSetT;

	typedef carl::FormulasMulti<Poly> FormulasMultiT;

    typedef carl::EvaluationMap<Rational> EvalRationalMap;
    
    typedef carl::Interval<Rational> RationalInterval;
    
    typedef carl::EvaluationMap<RationalInterval> EvalRationalIntervalMap;

    typedef carl::Interval<double> DoubleInterval;
    
    typedef carl::EvaluationMap<DoubleInterval> EvalDoubleIntervalMap;
    
    typedef carl::VarInfo<Poly> VarPolyInfo;
    
    typedef carl::VarInfoMap<Poly> VarPolyInfoMap;
    
    template<template<typename> class Operator>
    using Contractor = carl::Contraction<Operator, Poly>;
    
    typedef carl::Factors<Poly> Factorization;
    
    // Constants.
    ///@todo move static variables to own cpp
    static const Rational ZERO_RATIONAL = Rational( 0 );
    
    static const Rational ONE_RATIONAL = Rational( 1 );
    
    static const Rational MINUS_ONE_RATIONAL = Rational( -1 );
    
    static const Poly ZERO_POLYNOMIAL = Poly( ZERO_RATIONAL );
    
    static const Poly ONE_POLYNOMIAL = Poly( ONE_RATIONAL );
    
    static const Poly MINUS_ONE_POLYNOMIAL = Poly( MINUS_ONE_RATIONAL );
    
    static const unsigned MAX_DEGREE_FOR_FACTORIZATION = 6;
    
    static const unsigned MIN_DEGREE_FOR_FACTORIZATION = 2;
    
    static const unsigned MAX_DIMENSION_FOR_FACTORIZATION = 6;
    
    static const unsigned MAX_NUMBER_OF_MONOMIALS_FOR_FACTORIZATION = 7;
    
    // Macros.
    
    #define ANSWER_TO_STRING(_ans) (_ans == True ? "True" : (_ans == False ? "False" : (_ans == Unknown ? "Unknown" : "Undefined")))

    // Function wrapper.
    
    inline carl::Variable newVariable( const std::string& _name, const carl::VariableType& _type )
    {
        return carl::VariablePool::getInstance().getFreshVariable( _name, _type );
    }
    
    inline carl::Sort newSort( const std::string& _name, size_t _arity = 0 )
    {
        carl::SortManager::getInstance().declare( _name, _arity );
        return carl::SortManager::getInstance().getSort( _name );
    }
    
    inline carl::UninterpretedFunction newUF( const std::string& _name, std::vector<carl::Sort>&& _dom, const carl::Sort& _codom )
    {
        return carl::UFManager::getInstance().newUninterpretedFunction( _name, std::move(_dom), _codom );
    }
    
    inline carl::UninterpretedFunction newUF( const std::string& _name, const std::vector<carl::Sort>& _dom, const carl::Sort& _codom )
    {
        return newUF( _name, std::move( std::vector<carl::Sort>( _dom ) ), _codom );
    }
    
    inline carl::UninterpretedFunction newUF( const std::string& _name, const carl::Sort& _dom, const carl::Sort& _codom )
    {
        std::vector<carl::Sort> dom;
        dom.push_back( _dom );
        return smtrat::newUF( _name, std::move( dom ), _codom );
    }
    
    inline carl::UninterpretedFunction newUF( const std::string& _name, const carl::Sort& _domA, const carl::Sort& _domB, const carl::Sort& _codom )
    {
        std::vector<carl::Sort> dom;
        dom.push_back( _domA );
        dom.push_back( _domB );
        return smtrat::newUF(_name, std::move( dom ), _codom);
    }
    
    inline carl::UFInstance newUFInstance( const carl::UninterpretedFunction& _function, std::vector<carl::UVariable>&& _args )
    {
        return carl::UFInstanceManager::getInstance().newUFInstance( _function, std::move(_args) );
    }
    
    inline carl::UFInstance newUFInstance( const carl::UninterpretedFunction& _function, const std::vector<carl::UVariable>& _args )
    {
        return smtrat::newUFInstance( _function, std::move(std::vector<carl::UVariable>(_args)) );
    }
    
    inline carl::UFInstance newUFInstance( const carl::UninterpretedFunction& _function, const carl::UVariable& _arg )
    {   
        std::vector<carl::UVariable> args;
        args.push_back( _arg );
        return smtrat::newUFInstance( _function, std::move(args) );
    }
    
    inline carl::UFInstance newUFInstance( const carl::UninterpretedFunction& _function, const carl::UVariable& _argA, const carl::UVariable& _argB )
    {   
        std::vector<carl::UVariable> args;
        args.push_back( _argA );
        args.push_back( _argB );
        return smtrat::newUFInstance( _function, std::move(args) );
    }
    
}    // namespace smtrat

extern template class carl::Term<smtrat::Rational>;
extern template class carl::MultivariatePolynomial<smtrat::Rational>;
extern template class carl::Constraint<smtrat::Poly>;
extern template class carl::Formula<smtrat::Poly>;
extern template class carl::Interval<smtrat::Rational>;
extern template class carl::Interval<double>;

extern template class std::set<carl::Constraint<smtrat::Poly>, carl::less<carl::Constraint<smtrat::Poly>, false>>;
extern template class std::vector<carl::Formula<smtrat::Poly>>;
extern template class std::set<carl::Formula<smtrat::Poly>>;
extern template class std::multiset<carl::Formula<smtrat::Poly>, carl::less<carl::Formula<smtrat::Poly>>>;
extern template class std::map<carl::Variable,smtrat::Rational>;
extern template class std::map<carl::Variable,smtrat::RationalInterval>;
extern template class std::map<carl::Variable,smtrat::DoubleInterval>;
extern template class carl::VariableInformation<true, smtrat::Poly>;
extern template class std::map<carl::Variable, carl::VariableInformation<true, smtrat::Poly>>;
extern template class std::map<smtrat::Poly,carl::exponent>;