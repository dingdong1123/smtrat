/*
 *  SMT-RAT - Satisfiability-Modulo-Theories Real Algebra Toolbox
 * Copyright (C) 2012 Florian Corzilius, Ulrich Loup, Erika Abraham, Sebastian Junges
 *
 * This file is part of SMT-RAT.
 *
 * SMT-RAT is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SMT-RAT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SMT-RAT.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


/**
 * @file   GroebnerModule.tpp
 *
 * @author Sebastian Junges
 * @author Ulrich Loup
 *
 * @version 2012-12-20
 */
#include "../../config.h"

#include "GroebnerModule.h"
#include "GBModuleStatistics.h"
#include "UsingDeclarations.h"
#ifdef USE_NSS
#include <reallynull/lib/GroebnerToSDP/GroebnerToSDP.h>
#endif

//#define CHECK_SMALLER_MUSES
//#define SEARCH_FOR_RADICALMEMBERS
//#define GB_OUTPUT

using std::set;
using GiNaC::ex_to;

using GiNaCRA::VariableListPool;
using GiNaCRA::MultivariatePolynomialMR;

namespace smtrat
{

template<class Settings>
GroebnerModule<Settings>::GroebnerModule( ModuleType _type, const Formula* const _formula, RuntimeSettings* settings, Conditionals& _conditionals, Manager* const _manager ):
        Module( _type, _formula, _conditionals, _manager ),
    mBasis( ),
    mInequalities( this ),
    mStateHistory( ),
    mRecalculateGB(false),
    mRuntimeSettings(static_cast<GBRuntimeSettings*>(settings))
#ifdef SMTRAT_DEVOPTION_Statistics
    ,
    mStats(GroebnerModuleStats::getInstance(Settings::identifier)),
    mGBStats(GBCalculationStats::getInstance(Settings::identifier))
#endif
{
    pushBacktrackPoint( mpReceivedFormula->end( ) );
}

template<class Settings>
GroebnerModule<Settings>::~GroebnerModule( )
{

}

/**
 * Adds the constraint to the known constraints of the module.
 * This includes scanning variables as well as transforming inequalities, if this is enabled.
 * @param _formula A REALCONSTRAINT which should be regarded by the next theory call.
 * @return true
 */
template<class Settings>
bool GroebnerModule<Settings>::assertSubformula( Formula::const_iterator _formula )
{
    Module::assertSubformula( _formula );
    if( !(*_formula)->getType() == REALCONSTRAINT )
    {
        return true;
    }

    const Constraint& constraint = (*_formula)->constraint( );
    // add variables
    for( GiNaC::symtab::const_iterator it = constraint.variables( ).begin( ); it != constraint.variables( ).end( ); ++it )
    {
        VariableListPool::addVariable( ex_to<symbol > (it->second) );
        mListOfVariables.insert( *it );
    }

    #ifdef SMTRAT_DEVOPTION_Statistics
    mStats->constraintAdded(constraint.relation());
    #endif

    processNewConstraint(_formula);
    //only equalities should be added to the gb
    return true;

}

/**
 * Method which updates internal data structures to reflect the added formula.
 * @param _formula
 */
template<class Settings>
void GroebnerModule<Settings>::processNewConstraint(Formula::const_iterator _formula)
{
    const Constraint& constraint = (*_formula)->constraint( );
    bool toGb = (constraint.relation( ) == CR_EQ || Settings::transformIntoEqualities == ALL_INEQUALITIES || (Settings::transformIntoEqualities == ONLY_NONSTRICT && (constraint.relation( ) == CR_GEQ || constraint.relation( ) == CR_LEQ) ) );

    if( toGb )
    {
        handleConstraintToGBQueue(_formula);
    }
    else
    {
        handleConstraintNotToGB(_formula);
    }
}

/**
 * Method which adds constraint to the GB-process.
 * @param _formula
 */
template<class Settings>
void GroebnerModule<Settings>::handleConstraintToGBQueue(Formula::const_iterator _formula)
{
    pushBacktrackPoint( _formula );
    if((*_formula)->constraint( ).relation() == CR_EQ)
    {
        mBasis.addPolynomial( Polynomial( (*_formula)->constraint( ).lhs( ) ), mBacktrackPoints.size() - 2  );
    }
    else
    {
        mBasis.addPolynomial( transformIntoEquality( _formula ), mBacktrackPoints.size() - 2 );
    }
    saveState( );

    if( !Settings::passGB )
    {
        addReceivedSubformulaToPassedFormula( _formula );
    }
}


/**
 * Method which adds constraints to our internal datastructures without adding them to the GB. E.g. inequalities are added to the inequalitiestable.
 * @param _formula
 */
template<class Settings>
void GroebnerModule<Settings>::handleConstraintNotToGB(Formula::const_iterator _formula)
{
    if( Settings::checkInequalities == NEVER )
    {
        addReceivedSubformulaToPassedFormula( _formula );
    }
    else if( Settings::checkInequalities == ALWAYS )
    {
        mNewInequalities.push_back( mInequalities.InsertReceivedFormula( _formula ) );
        assert((*(mNewInequalities.back()->first))->constraint().relation() != CR_EQ);
    }
    else
    {
        assert( Settings::checkInequalities == AFTER_NEW_GB);
        mInequalities.InsertReceivedFormula( _formula );
    }
}


/**
 * A theory call to the GroebnerModule. The exact working of this module depends on the settings in GBSettings.
 * @return (TRUE,FALSE,UNKNOWN) dependent on the asserted constraints.
 */
template<class Settings>
Answer GroebnerModule<Settings>::isConsistent( )
{
#ifdef GB_OUTPUT
    std::cout << "GB Called" << std::endl;
#endif
    // We can only handle conjunctions of constraints.
    if(!mpReceivedFormula->isRealConstraintConjunction())
    {
        return foundAnswer( Unknown );
    }
    // This check asserts that all the conflicts are handled by the SAT solver. (workaround)
    if( !mInfeasibleSubsets.empty() )
    {
        return foundAnswer( False );
    }

    #ifdef SMTRAT_DEVOPTION_Statistics
    mStats->called();
    #endif

    assert( mInfeasibleSubsets.empty( ) );

    // New elements queued for adding to the gb have to be handled.
    if( !mBasis.inputEmpty( ) )
    {
        #ifdef GB_OUTPUT
        std::cout << "Scheduled: " << std::endl;
        mBasis.printScheduledPolynomials();
        #endif
        if(Settings::iterativeVariableRewriting)
        {
            if(mRewriteRules.size() > 0)
            {
                std::list<std::pair<GiNaCRA::BitVector, GiNaCRA::BitVector> > deductions;
                deductions = mBasis.applyVariableRewriteRulesToInput(mRewriteRules);
                knownConstraintDeduction(deductions);
            }
        }
        #ifdef GB_OUTPUT
        std::cout << "-------->" << std::endl;
        mBasis.printScheduledPolynomials();
        std::cout << "--------|" << std::endl;
        #endif
        //first, we interreduce the input!
    }

    if( !mBasis.inputEmpty( ) )
    {
        std::list<std::pair<GiNaCRA::BitVector, GiNaCRA::BitVector> > deduced = mBasis.reduceInput( );
        //analyze for deductions
        knownConstraintDeduction(deduced);
    }

    //If the GB needs to be updated, we do so. Otherwise we skip.
    // Notice that we might to update the gb after backtracking (mRecalculateGB flag).
    if( !mBasis.inputEmpty( ) || (mRecalculateGB && mBacktrackPoints.size() > 1 ) )
    {
        //now, we calculate the groebner basis
#ifdef GB_OUTPUT
        std::cout << "basis calculate call" << std::endl;
#endif
        mBasis.calculate( );
#ifdef GB_OUTPUT
        std::cout << "basis calculated" << std::endl;
#endif
        mRecalculateGB = false;
        if( Settings::iterativeVariableRewriting && !mBasis.isConstant( ) )
        {
            iterativeVariableRewriting();
        }

        Polynomial witness;
        #ifdef USE_NSS
        // On linear systems, all solutions lie in Q. So we do not have to check for a solution.
        if( Settings::applyNSS && !mBasis.isConstant( ) && !mBasis.getGbIdeal( ).isLinear( ) )
        {
            using namespace reallynull;
            std::cout << "NSS..?" << std::flush;
            // Lets search for a witness. We only have to do this if the gb is non-constant.

            std::set<unsigned> variables;
            std::set<unsigned> allVars = mBasis.getGbIdeal( ).gatherVariables( );
            std::set<unsigned> superfluous = mBasis.getGbIdeal( ).getSuperfluousVariables( );
            std::set_difference( allVars.begin( ), allVars.end( ),
                                superfluous.begin( ), superfluous.end( ),
                                std::inserter( variables, variables.end( ) ) );

            unsigned vars = variables.size( );
            // We currently only try with a low nr of variables.
            if( vars < Settings::SDPupperBoundNrVariables )
            {
                std::cout << " Run SDP.." << std::flush;

                GroebnerToSDP<typename Settings::Order> sdp( mBasis.getGbIdeal( ), MonomialIterator( variables, Settings::maxSDPdegree ) );
                witness = sdp.findWitness( );
            }
            std::cout << std::endl;
            if( !witness.isZero( ) ) std::cout << "Found witness: " << witness << std::endl;
        }
        // We have found an infeasible subset. Generate it.
        #endif
        if( mBasis.isConstant( ) || (Settings::applyNSS && !witness.isZero( )) )
        {
            if( mBasis.isConstant( ) )
            {
                #ifdef SMTRAT_DEVOPTION_Statistics
                mStats->constantGB();
                #endif
                witness = mBasis.getGb( ).front( );
            }
            #ifdef USE_NSS
            else
            {
                typename Settings::Reductor red( mBasis.getGbIdeal( ), witness );
                witness = red.fullReduce( );
                std::cout << witness << std::endl;
                assert( witness.isZero( ) );
            }
            #endif
            mInfeasibleSubsets.push_back( set<const Formula*>() );
            // The equalities we used for the basis-computation are the infeasible subset

            GiNaCRA::BitVector::const_iterator origIt = witness.getOrigins( ).getBitVector( ).begin( );
            auto it = mBacktrackPoints.begin( );
            for( ++it; it != mBacktrackPoints.end( ); ++it )
            {
                assert(it != mBacktrackPoints.end());

                assert( (**it)->getType( ) == REALCONSTRAINT );
                assert( Settings::transformIntoEqualities != NO_INEQUALITIES || (**it)->constraint( ).relation( ) == CR_EQ );

                if( Settings::getReasonsForInfeasibility )
                {
                    if( origIt.get( ) )
                    {
                        mInfeasibleSubsets.back( ).insert( **it );
                    }
                    origIt++;
                }
                else
                {
                    mInfeasibleSubsets.back( ).insert( **it );
                }
            }

            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->EffectivenessOfConflicts(mInfeasibleSubsets.back().size()/mpReceivedFormula->size());
            #endif

            #ifdef CHECK_SMALLER_MUSES
            unsigned infsubsetsize = mInfeasibleSubsets.front().size();
            if(infsubsetsize > 1) {
                std::vector<Formula> infsubset = generateSubformulaeOfInfeasibleSubset(0, infsubsetsize-1);
                storeSmallerInfeasibleSubsetsCheck(infsubset);
            }

            #endif
            return foundAnswer( False );
        }
        saveState( );


        if( Settings::checkInequalities != NEVER )
        {
            Answer ans = Unknown;
            ans = mInequalities.reduceWRTGroebnerBasis( mBasis.getGbIdeal( ), mRewriteRules );

            if( ans == False )
            {
                return foundAnswer( ans );
            }
        }
        assert( mInfeasibleSubsets.empty( ) );


        // When passing a gb, first remove last and then pass current gb.
        if( Settings::passGB )
        {
            for( Formula::iterator i = mpPassedFormula->begin( ); i != mpPassedFormula->end( ); )
            {
                assert( (*i)->getType( ) == REALCONSTRAINT );
                if( (*i)->constraint( ).relation( ) == CR_EQ )
                {
                    i = super::removeSubformulaFromPassedFormula( i );
                }
                else
                {
                    ++i;
                }
            }

            passGB( );
        }
    }
    // If we always want to check inequalities, we also have to do so when there is no new groebner basis
    else if( Settings::checkInequalities == ALWAYS )
    {
        Answer ans = Unknown;
        // We only check those inequalities which are new, as the others are unchanged and have already been reduced wrt the latest GB
        ans = mInequalities.reduceWRTGroebnerBasis( mNewInequalities, mBasis.getGbIdeal( ), mRewriteRules );
        // New inequalities are handled now, no need to longer save them as new.
        mNewInequalities.clear( );
        // If we managed to get an answer, we can return that.
        if( ans != Unknown )
        {
            return foundAnswer( ans );
        }
    }
    assert( mInfeasibleSubsets.empty( ) );

    #ifdef GB_OUTPUT
    printRewriteRules();
    mInequalities.print();
    std::cout << "Basis" << std::endl;
    mBasis.getGbIdeal().print();
    print();
    #endif


    // call other modules as the groebner module cannot decide satisfiability.
    Answer ans = runBackends( );
    if( ans == False )
    {
        #ifdef SMTRAT_DEVOPTION_Statistics
        mStats->backendFalse();
        #endif
        // use the infeasible subsets from our backends.
        getInfeasibleSubsets( );

        assert( !mInfeasibleSubsets.empty( ) );
    }
    return foundAnswer( ans );
}


/**
 * With the new groebner basis, we search for radical-members which then can be added to the GB.
 * @return
 */

template<class Settings>
bool GroebnerModule<Settings>::iterativeVariableRewriting()
{
    std::list<Polynomial> polynomials = mBasis.getGb();
    bool newRuleFound = true;
    bool gbUpdate = false;

    // The parameters of the new rule.
    unsigned ruleVar;
    Term ruleTerm;
    GiNaCRA::BitVector ruleReasons;

    std::map<unsigned, std::pair<Term, BitVector> >& rewrites = mRewriteRules;
    GiNaCRA::Buchberger<typename Settings::Order> basis;

    while(newRuleFound)
    {
        newRuleFound = false;
#ifdef GB_OUTPUT
        std::cout << "current gb" << std::endl;
        for(typename std::list<Polynomial>::const_iterator it = polynomials.begin(); it != polynomials.end(); ++it )
        {
            std::cout << *it;
            it->getOrigins().getBitVector().print();
            std::cout << std::endl;
        }
        std::cout << "----" << std::endl;
#endif

        for(typename std::list<Polynomial>::iterator it = polynomials.begin(); it != polynomials.end();)
        {
            if( it->nrOfTerms() == 1 && it->lterm().tdeg()==1 )
            {
                //TODO optimization, this variable does not appear in the gb.
                ruleVar = it->lterm().getSingleVariableNr();
                ruleTerm = Term(0);
                ruleReasons = it->getOrigins().getBitVector();
                newRuleFound = true;
            }
            else if( it->nrOfTerms() == 2 )
            {
                if(it->lterm().tdeg() == 1 )
                {
                    ruleVar = it->lterm().getSingleVariableNr();
                    if( it->trailingTerm().hasVariable(ruleVar) )
                    {
                        // TODO deduce a factorisation.
                    }
                    else
                    {
                        // learned a rule.
                        ruleTerm = -1 * it->trailingTerm();
                        ruleReasons = it->getOrigins().getBitVector();
                        newRuleFound = true;
                    }
                }
                else if(it->trailingTerm().tdeg() == 1 )
                {
                    ruleVar = it->trailingTerm().getSingleVariableNr();
                    if( it->lterm().hasVariable(ruleVar) )
                    {
                        // TODO deduce a factorisation
                    }
                    else
                    {
                        // learned a rule.
                        ruleTerm = it->lterm().divide(-it->trailingTerm().getCoeff());
                        ruleReasons = it->getOrigins().getBitVector();
                        newRuleFound = true;
                    }
                }
            }
            if(newRuleFound)
            {
                it = polynomials.erase(it);
                break;
            }
            else
            {
                ++it;
            }
        }

        if(newRuleFound)
        {
            gbUpdate = true;
            rewrites.insert(std::pair<unsigned, std::pair<Term, BitVector> >(ruleVar, std::pair<Term, BitVector>(ruleTerm, ruleReasons ) ) );

            std::list<Polynomial> resultingGb;
            basis = GiNaCRA::Buchberger<typename Settings::Order>();
            for(typename std::list<Polynomial>::const_iterator it = polynomials.begin(); it != polynomials.end(); ++it )
            {
                resultingGb.push_back(it->rewriteVariables(ruleVar, ruleTerm, ruleReasons));
                basis.addPolynomial(resultingGb.back(), false);
                #ifdef GB_OUTPUT
                std::cout << *it << " ---- > ";
                std::cout.flush();
                std::cout << resultingGb.back() << std::endl;
                #endif
            }

            if( !basis.inputEmpty( ) )
            {
                basis.reduceInput();
                basis.calculate();
                polynomials = basis.getGb();
            }

            for( std::map<unsigned, std::pair<Term, BitVector> >::iterator it = rewrites.begin(); it != rewrites.end(); ++it )
            {
                std::pair<Term, bool> reducedRule = it->second.first.rewriteVariables(ruleVar, ruleTerm);
                if(reducedRule.second)
                {
                    it->second.first = reducedRule.first;
                    it->second.second |= ruleReasons;
                }
            }
            #ifdef GB_OUTPUT
            printRewriteRules();
            #endif

        }
    }

    if( gbUpdate )
    {
        mBasis = basis;
        saveState();
    }

    #ifdef SEARCH_FOR_RADICALMEMBERS
    std::set<unsigned> variableNumbers(mBasis.getGbIdeal().gatherVariables());

    //find variable rewrite rules
    //apply the rules RRI-* from the Thesis from G.O. Passmore
    // Iterate over all variables in the GB
    for(std::set<unsigned>::const_iterator it =  variableNumbers.begin(); it != variableNumbers.end(); ++it) {
        // We search until a given (static) maximal exponent
        for(unsigned exponent = 3; exponent <= 17; ++(++exponent) )
        {
            // Construct x^exp
            Term t(Rational(1), *it, exponent);
            Polynomial reduce(t);

            // reduce x^exp
            typename Settings::Reductor reduction( mBasis.getGbIdeal( ), reduce );
            reduce = reduction.fullReduce( );

            if( reduce.isConstant() )
            {
                // TODO handle 0 and 1.
                // TODO handle other cases
                // calculate q-root(reduce);
                #ifdef SMTRAT_DEVOPTION_Statistics
                mStats->FoundEqualities();
                #endif
                std::cout << t << " -> " << reduce << std::endl;

                break;
            }
            //x^(m+1) - y^(n+1)
            else if( reduce.isReducedIdentity(*it, exponent))
            {
                #ifdef SMTRAT_DEVOPTION_Statistics
                mStats->FoundIdentities();
                #endif
                std::cout << t << " -> " << reduce << std::endl;

                break;
            }
        }
    }


    #else
    return false;
    #endif
}

template<class Settings>
void GroebnerModule<Settings>::knownConstraintDeduction(const std::list<std::pair<GiNaCRA::BitVector,GiNaCRA::BitVector> >& deductions)
{
    for(auto it =  deductions.rbegin(); it != deductions.rend(); ++it)
    {
        // if the bitvector is not empty, there is a theory deduction
        if( Settings::addTheoryDeductions == ALL_CONSTRAINTS && !it->second.empty() )
        {
            Formula* deduction = new Formula(OR);
            std::set<const Formula*> deduced( generateReasons( it->first ));
            // When this kind of deduction is greater than one, we would have to determine wich of them is really the deduced one.
            if( deduced.size() > 1 ) continue;
            std::set<const Formula*> originals( generateReasons( it->second ));
            std::set<const Formula*> originalsWithoutDeduced;

            std::set_difference(originals.begin(), originals.end(), deduced.begin(), deduced.end(), std::inserter(originalsWithoutDeduced, originalsWithoutDeduced.end()));


            for( auto jt =  originalsWithoutDeduced.begin(); jt != originalsWithoutDeduced.end(); ++jt )
            {
                deduction->addSubformula( new Formula( NOT ) );
                deduction->back()->addSubformula( (*jt)->pConstraint() );
            }

            for( auto jt =  deduced.begin(); jt != deduced.end(); ++jt )
            {
                deduction->addSubformula( (*jt)->pConstraint() );
            }

            addDeduction(deduction);
            //deduction->print();
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->DeducedEquality();
            #endif
        }
    }
}

template<class Settings>
void GroebnerModule<Settings>::newConstraintDeduction( )
{


}

/**
 * Removes the constraint from the GBModule.
 * Notice: Whenever a constraint is removed which was not asserted before, this leads to an unwanted error.
 *    A general approach for this has to be found.
 * @param _formula the constraint which should be removed.
 */
template<class Settings>
void GroebnerModule<Settings>::removeSubformula( Formula::const_iterator _formula )
{
    if((*_formula)->getType() != REALCONSTRAINT) {
        super::removeSubformula( _formula );
        return;
    }
    #ifdef SMTRAT_DEVOPTION_Statistics
    mStats->constraintRemoved((*_formula)->constraint().relation());
    #endif
    if( Settings::transformIntoEqualities == ALL_INEQUALITIES )
    {
        popBacktrackPoint( _formula );
    }
    else if( Settings::transformIntoEqualities == ONLY_NONSTRICT && (*_formula)->constraint( ).relation( ) != CR_GREATER && (*_formula)->constraint( ).relation( ) != CR_LESS )
    {
        popBacktrackPoint( _formula );
    }
    else if( (*_formula)->constraint( ).relation( ) == CR_EQ )
    {
        popBacktrackPoint( _formula );
    }
    else
    {
        if( Settings::checkInequalities != NEVER )
        {
            if (Settings::checkInequalities ==  ALWAYS)
            {
                removeReceivedFormulaFromNewInequalities( _formula );
            }
            mInequalities.removeInequality( _formula );
        }
    }
    super::removeSubformula( _formula );
}

/**
 * Removes a received formula from the list of new inequalities. It assumes that there is only one such element in the list.
 * @param _formula
 */
template<class Settings>
void GroebnerModule<Settings>::removeReceivedFormulaFromNewInequalities( Formula::const_iterator _formula )
{
    for(auto it = mNewInequalities.begin(); it != mNewInequalities.end(); ++it )
    {
        if((*it)->first == _formula)
        {
            mNewInequalities.erase(it);
            return;
        }
    }
}

/**
 * To implement backtrackability, we save the current state after each equality we add.
 * @param btpoint The equality we have removed
 */
template<class Settings>
void GroebnerModule<Settings>::pushBacktrackPoint( Formula::const_iterator btpoint )
{
    assert( mBacktrackPoints.empty( ) || (*btpoint)->getType( ) == REALCONSTRAINT );
    assert( mBacktrackPoints.size( ) == mStateHistory.size( ) );

    // We save the current level
    if( !mBacktrackPoints.empty( ) )
    {
        saveState( );
    }

    if( mStateHistory.empty() )
    {
        // there are no variable rewrite rules, so we can only push our current basis and empty rewrites
        mStateHistory.push_back( GroebnerModuleState<Settings>( mBasis, std::map<unsigned, std::pair<Term, GiNaCRA::BitVector> >() ) );
    }
    else
    {
        // we save the current basis and the rewrite rules
        mStateHistory.push_back( GroebnerModuleState<Settings>( mBasis, mRewriteRules ) );
    }

    mBacktrackPoints.push_back( btpoint );
    assert( mBacktrackPoints.size( ) == mStateHistory.size( ) );

    // If we also handle inequalities in the inequalitiestable, we have to notify it about the extra pushbacktrack point
    if( Settings::checkInequalities != NEVER )
    {
        mInequalities.pushBacktrackPoint( );
    }
}


/**
 * Pops all states from the stack until the state which we had before the constraint was added.
 * Then, we make new states with all equalities which were added afterwards.
 * @param a pointer in the received formula to the constraint which will be removed.
 */
template<class Settings>
void GroebnerModule<Settings>::popBacktrackPoint( Formula::const_iterator btpoint )
{
    //assert( validityCheck( ) );
    assert( mBacktrackPoints.size( ) == mStateHistory.size( ) );
    assert( !mBacktrackPoints.empty( ) );

    //We first count how far we have to backtrack.
    //Because the polynomials have to be added again afterwards, we save them in a list.
    unsigned nrOfBacktracks = 1;
    std::list<Formula::const_iterator> rescheduled;
    //TODO efficiency improvement by removing at once.
    while( !mBacktrackPoints.empty( ) )
    {
        if( mBacktrackPoints.back( ) == btpoint )
        {
            mBacktrackPoints.pop_back( );
            break;
        }
        else
        {
            ++nrOfBacktracks;
            rescheduled.push_front(mBacktrackPoints.back());
            mBacktrackPoints.pop_back( );
        }
    }

    #ifdef SMTRAT_DEVOPTION_Statistics
    mStats->PopLevel(nrOfBacktracks);
    #endif

    for( unsigned i = 0; i < nrOfBacktracks; ++i )
    {
        assert( !mStateHistory.empty( ) );
        mStateHistory.pop_back( );
    }
    assert( mBacktrackPoints.size( ) == mStateHistory.size( ) );
    assert( !mStateHistory.empty( ) );

    // Load the state to be restored;
    mBasis = mStateHistory.back( ).getBasis( );
    mRewriteRules = mStateHistory.back().getRewriteRules();
    //assert( mBasis.nrOriginalConstraints( ) == mBacktrackPoints.size( ) - 1 );

    if( Settings::checkInequalities != NEVER )
    {
        mInequalities.popBacktrackPoint( nrOfBacktracks );
    }

    mRecalculateGB = true;
    //Add all others
    for( auto it = rescheduled.begin(); it != rescheduled.end(); ++it )
    {
        assert( (**it)->getType( ) == REALCONSTRAINT );
        Constraint_Relation relation = (**it)->constraint( ).relation( );
        bool isInGb = Settings::transformIntoEqualities == ALL_INEQUALITIES || relation == CR_EQ
                || (Settings::transformIntoEqualities == ONLY_NONSTRICT && relation != CR_GREATER && relation != CR_LESS);
        if( isInGb )
        {
            pushBacktrackPoint( *it );
            mBasis.addPolynomial( (relation == CR_EQ ? Polynomial( (**it)->constraint( ).lhs( ) ) : transformIntoEquality( *it )), mBacktrackPoints.size()-2 );
            // and save them
            saveState( );
        }
    }
    //assert( mBasis.nrOriginalConstraints( ) == mBacktrackPoints.size( ) - 1 );
}

/**
 * Transforms a given inequality to a polynomial such that p = 0 is equivalent to the given constraint.
 * This is done by inserting an additional variable which has an index which is given by the id of the given inequality.
 * @param constraint a pointer to the inequality
 * @return The polynomial which represents the equality.
 */
template<class Settings>
typename GroebnerModule<Settings>::Polynomial GroebnerModule<Settings>::transformIntoEquality( Formula::const_iterator constraint )
{
    Polynomial result( (*constraint)->constraint( ).lhs( ) );
    unsigned constrId = (*constraint)->constraint( ).id( );
    std::map<unsigned, unsigned>::const_iterator mapentry = mAdditionalVarMap.find( constrId );
    unsigned varNr;
    if( mapentry == mAdditionalVarMap.end( ) )
    {
        std::stringstream stream;
        stream << "AddVarGB" << constrId;
        GiNaC::symbol varSym = ex_to<symbol > (Formula::newRealVariable( stream.str( ) ));
        mListOfVariables[stream.str()] = varSym;
        varNr = VariableListPool::addVariable( varSym );
        mAdditionalVarMap.insert(std::pair<unsigned, unsigned>(constrId, varNr));
    }
    else
    {
        varNr = mapentry->second;
    }

    // Modify to reflect inequalities.
    switch( (*constraint)->constraint( ).relation( ) )
    {
    case CR_GEQ:
        result = result + GiNaCRA::MultivariateTermMR( -1, varNr, 2 );
        break;
    case CR_LEQ:
        result = result + GiNaCRA::MultivariateTermMR( 1, varNr, 2 );
        break;
    case CR_GREATER:
        result = result * GiNaCRA::MultivariateTermMR( 1, varNr, 2 );
        result = result + GiNaCRA::MultivariateTermMR( -1 );
        break;
    case CR_LESS:
        result = result * GiNaCRA::MultivariateTermMR( 1, varNr, 2 );
        result = result + GiNaCRA::MultivariateTermMR( 1 );
        break;
    case CR_NEQ:
        result = result * GiNaCRA::MultivariateTermMR( 1, varNr, 1);
        result = result + GiNaCRA::MultivariateTermMR( 1 );
        break;
    default:
        assert( false );
    }
    return result;
}

/**
 *
 * @return
 */
template<class Settings>
bool GroebnerModule<Settings>::saveState( )
{
    assert( mStateHistory.size( ) == mBacktrackPoints.size( ) );

    // TODO fix this copy.
    mStateHistory.pop_back( );
    mStateHistory.push_back( GroebnerModuleState<Settings>( mBasis, mRewriteRules ) );

    return true;
}

/**
 * Add the equalities from the Groebner basis to the passed formula. Adds the reason vector.
 */
template<class Settings>
void GroebnerModule<Settings>::passGB( )
{
    assert( Settings::passGB );
    vec_set_const_pFormula originals;
    originals.push_back( set<const Formula*>() );

    if( !Settings::passWithMinimalReasons )
    {
        // find original constraints which made the gb.
        for( Formula::const_iterator it = mpReceivedFormula->begin( ); it != mpReceivedFormula->end( ); ++it )
        {
            if( Settings::transformIntoEqualities == ALL_INEQUALITIES )
            {
                originals.front( ).insert( *it );
            }
            else if( Settings::transformIntoEqualities == ONLY_NONSTRICT && (*it)->constraint( ).relation( ) != CR_GREATER && (*it)->constraint( ).relation( ) != CR_LESS )
            {
                originals.front( ).insert( *it );
            }
            else if( (*it)->constraint( ).relation( ) == CR_EQ )
            {
                originals.front( ).insert( *it );
            }
        }
    }

    //std::cout << "Passing gb" << std::endl;
    // The gb should be passed
    std::list<Polynomial> simplified = mBasis.getGb( );
    for( typename std::list<Polynomial>::const_iterator simplIt = simplified.begin( ); simplIt != simplified.end( ); ++simplIt )
    {
        //std::cout << *simplIt << std::endl;
        if( Settings::passWithMinimalReasons )
        {
            originals.front( ) = generateReasons( simplIt->getOrigins( ).getBitVector( ) );
        }
        assert( !originals.front( ).empty( ) );
        //TODO: replace "Formula::constraintPool().variables()" by a smaller approximations of the variables contained in "simplIt->toEx( )"
        addSubformulaToPassedFormula( new Formula( Formula::newConstraint( simplIt->toEx( ), CR_EQ, Formula::constraintPool().realVariables() ) ), originals );
    }
}

/**
 * Generate reason sets from reason vectors
 * @param reasons The reasons vector.
 * @return The reason set.
 */
template<class Settings>
std::set<const Formula*> GroebnerModule<Settings>::generateReasons( const GiNaCRA::BitVector& reasons )
{
    if(reasons.empty())
    {
        return std::set<const Formula*>();
    }
    GiNaCRA::BitVector::const_iterator origIt = reasons.begin( );
    std::set<const Formula*> origins;

    auto it = mBacktrackPoints.begin( );
    for( ++it; it != mBacktrackPoints.end( ); ++it )
    {
        assert( (**it)->getType( ) == REALCONSTRAINT );
        assert( Settings::transformIntoEqualities != NO_INEQUALITIES || (**it)->constraint( ).relation( ) == CR_EQ );
        //
        if( origIt.get( ) )
        {
            origins.insert( **it );
        }
        origIt++;
    }
    return origins;

}

/**
 *  Prints the state history.
 */

template<class Settings>
void GroebnerModule<Settings>::printStateHistory( )
{
    std::cout << "[";
    auto btp = mBacktrackPoints.begin( );
    for( auto it = mStateHistory.begin( ); it != mStateHistory.end( ); ++it )
    {
        std::cout << **btp << ": ";
        it->getBasis( ).getGbIdeal( ).print( );
        std::cout << "," << std::endl;
        btp++;
    }
    std::cout << "]" << std::endl;
}

template<class Settings>
void GroebnerModule<Settings>::printRewriteRules( )
{
    for(auto it = mRewriteRules.begin(); it != mRewriteRules.end(); ++it)
    {
        std::cout << it->first << " -> " << it->second.first << " [";
        it->second.second.print();
        std::cout <<  "]" << std::endl;
    }
}


/**
 * A validity check of the data structures which can be used to assert valid behaviour.
 * @return true, iff the backtrackpoints are valid.
 */

template<class Settings>
bool GroebnerModule<Settings>::validityCheck( )
{
    auto btp = mBacktrackPoints.begin( );
    ++btp;
    for( auto it = mpReceivedFormula->begin( ); it != mpReceivedFormula->end( ); ++it )
    {
        bool isInGb = Settings::transformIntoEqualities == ALL_INEQUALITIES || (*it)->constraint( ).relation( ) == CR_EQ
                || (Settings::transformIntoEqualities == ONLY_NONSTRICT && (*it)->constraint( ).relation( ) != CR_GREATER && (*it)->constraint( ).relation( ) != CR_LESS);

        if( isInGb )
        {
            if( it != *btp )
            {
//                print( );
//                printStateHistory( );
//                std::cout << *it << " (Element in received formula) != " << **btp << "(Backtrackpoint)" << std::endl;
                return false;
            }
            ++btp;
        }
    }
    return true;
}

/**
 * This function is overwritten such that it is visible to the InequalitiesTable. For more details take a look at Module::removeSubformulaFromPassedFormula()
 * @param _formula
 */

template<class Settings>
void GroebnerModule<Settings>::removeSubformulaFromPassedFormula( Formula::iterator _formula )
{
    //std::cout << "formula remove: ";
    //(**_formula).print();
    //std::cout << "\n";
    super::removeSubformulaFromPassedFormula( _formula );
}

/**
 * Initializes the inequalities table
 * @param module
 */
template<class Settings>
InequalitiesTable<Settings>::InequalitiesTable( GroebnerModule<Settings>* module ) : mModule( module )
{
    mBtnumber = 0;
    mNewConstraints = mReducedInequalities.begin( );
    #ifdef SMTRAT_DEVOPTION_Statistics
    mStats = GroebnerModuleStats::getInstance(Settings::identifier);
    #endif
}

/**
 * Adds the constraint as a row to the inequalities table.
 * @param received A pointer from the receivedFormula to the inequality.
 * @return The position in the inequalities table.
 */

template<class Settings>
typename InequalitiesTable<Settings>::Rows::iterator InequalitiesTable<Settings>::InsertReceivedFormula( Formula::const_iterator received )
{
    assert( (*received)->constraint().relation() != CR_EQ );
    mModule->addReceivedSubformulaToPassedFormula( received );
    // We assume that the just added formula is the last one.
    const Formula::iterator passedEntry = mModule->mpPassedFormula->last( );
    // And we add a row to our table
    return mReducedInequalities.insert( Row( received, RowEntry( passedEntry, (*received)->constraint( ).relation( ), std::list<CellEntry > (1, CellEntry( 0, Polynomial( (*received)->constraint( ).lhs( ) ) )) ) ) ).first;
}

/**
 * Informs the inequalities table that new reductions are with respect to the GB with the latest btpoint.
 */

template<class Settings>
void InequalitiesTable<Settings>::pushBacktrackPoint( )
{
    ++mBtnumber;
    if( Settings::setCheckInequalitiesToBeginAfter > 1 )
    {
        if( mLastRestart + Settings::setCheckInequalitiesToBeginAfter == mBtnumber )
        {
            mNewConstraints = mReducedInequalities.begin( );
        }
    }
}

/**
 * Clears cells from the inequalities table with backtrack points from the latest nrOfBacktracks many backtrackpoints.
 * Also updates the new backtracknumber.
 * @param nrOfBacktracks How many backtrack points are popped.
 */

template<class Settings>
void InequalitiesTable<Settings>::popBacktrackPoint( unsigned nrOfBacktracks )
{
    assert( mBtnumber >= nrOfBacktracks );
    mBtnumber -= nrOfBacktracks;
    for( auto it = mReducedInequalities.begin( ); it != mReducedInequalities.end( ); ++it )
    {
        typename std::list<CellEntry>::iterator listEnd = std::get < 2 > (it->second).end( );
        for( typename std::list<CellEntry>::iterator jt = std::get < 2 > (it->second).begin( ); jt != listEnd; ++jt )
        {
            if( jt->first > mBtnumber )
            {
                std::get < 2 > (it->second).erase( jt, listEnd );
                bool pass;
                if( Settings::passInequalities == FULL_REDUCED_IF )
                {
                    pass = Settings::passPolynomial::evaluate( std::get < 2 > (it->second).front( ).second, std::get < 2 > (it->second).back( ).second );
                }

                // what shall we pass
                if( Settings::passInequalities == AS_RECEIVED )
                {
                    if( std::get < 0 > (it->second) == mModule->mpPassedFormula->end( ) )
                    {
                        // we had reduced it to true, therefore not passed it, but now we have to pass the original one again.
                        mModule->addReceivedSubformulaToPassedFormula( it->first );
                        std::get < 0 > (it->second) = mModule->mpPassedFormula->last( );
                    }
                }
                else
                {
                    if( std::get < 0 > (it->second) != mModule->mpPassedFormula->end( ) )
                    {
                        // we can of course only remove something which is in the formula.
                        mModule->removeSubformulaFromPassedFormula( std::get < 0 > (it->second) );
                    }
                    if( Settings::passInequalities == FULL_REDUCED || (Settings::passInequalities == FULL_REDUCED_IF && pass) )
                    {
                        std::vector<std::set<const Formula*> > originals;
                        originals.push_back( mModule->generateReasons( std::get < 2 > (it->second).back( ).second.getOrigins( ).getBitVector( ) ) );
                        originals.front( ).insert( *(it->first) );
                        //TODO: replace "Formula::constraintPool().variables()" by a smaller approximations of the variables contained in "std::get < 2 > (it->second).back( ).second.toEx( )"
                        mModule->addSubformulaToPassedFormula( new Formula( Formula::newConstraint( std::get < 2 > (it->second).back( ).second.toEx( ), std::get < 1 > (it->second), Formula::constraintPool().realVariables() ) ), originals );

                    }
                    else
                    {
                        assert( Settings::passInequalities == FULL_REDUCED_IF );
                        //we pass the original one.
                        mModule->addReceivedSubformulaToPassedFormula( it->first );
                    }
                    //update the reference to the passed formula again
                    std::get < 0 > (it->second) = mModule->mpPassedFormula->last( );
                }
                break;
            }
        }
    }
}

/**
 * Reduce all rows with respect to the given Groebner basis.
 * @param gb The groebner basis
 * @return If one of the inequalities yields a contradiction, False, else Unknown.
 */

template<class Settings>
Answer InequalitiesTable<Settings>::reduceWRTGroebnerBasis( const Ideal& gb, const RewriteRules& rules )
{
    for( auto it = mReducedInequalities.begin( ); it != mReducedInequalities.end( ); ++it )
    {
        // The formula is not passed because it is unsatisfiable.
        if( !reduceWRTGroebnerBasis( it, gb, rules ) ) {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            if( Settings::withInfeasibleSubset == RETURN_DIRECTLY )
            {
                return False;
            }
        }
    }
    if( Settings::withInfeasibleSubset != RETURN_DIRECTLY )
    {
        if( mModule->mInfeasibleSubsets.empty( ) )
        {
            return Unknown;
        }
        else
        {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            return False;
        }
    }
    else
    {
        return Unknown;
    }

}

/**
 * Reduce the given rows with respect to the given Groebner basis.
 * @param ineqToBeReduced A list of rows which should be updated.
 * @param gb The Groebner basis.
 * @return If one of the inequalities yields a contradiction, False, else Unknown.
 */

template<class Settings>
Answer InequalitiesTable<Settings>::reduceWRTGroebnerBasis( const std::list<typename Rows::iterator>& ineqToBeReduced, const Ideal& gb, const RewriteRules& rules )
{
    for( auto it = ineqToBeReduced.begin( ); it != ineqToBeReduced.end( ); ++it )
    {
        assert( std::get < 1 > ((*it)->second) != CR_EQ );
        if( !reduceWRTGroebnerBasis( *it, gb, rules ) ) {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            if( Settings::withInfeasibleSubset == RETURN_DIRECTLY )
            {
                return False;
            }
        }
    }
    if( Settings::withInfeasibleSubset != RETURN_DIRECTLY )
    {
        if( mModule->mInfeasibleSubsets.empty( ) )
        {
            return Unknown;
        }
        else
        {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            return False;
        }
    }
    else
    {
        return Unknown;
    }
}

/**
 * Reduce the given row with respect to the given Groebner basis.
 * @param ineqToBeReduced A pointer to the row which should be updated.
 * @param gb The Groebner basis.
 * @return If one of the inequalities yields a contradiction, false, else true.
 */

template<class Settings>
bool InequalitiesTable<Settings>::reduceWRTGroebnerBasis( typename Rows::iterator it, const Ideal& gb, const RewriteRules& rules )
{
    assert( std::get < 1 > (it->second) != CR_EQ );

    Polynomial& p = std::get<2>(it->second).back( ).second;
    Polynomial reduced;

    bool reductionOccured = false;
    bool rewriteOccured = false;
    if( !p.isZero( ) && !p.isConstant( ) )
    {
        if(rules.size() == 0)
        {
            GiNaCRA::BaseReductor<typename Settings::Order> reductor( gb, p );
            reduced = reductor.fullReduce( );
            reductionOccured = reductor.reductionOccured( );
        }
        else
        {
            Polynomial ptemp = p.rewriteVariables(rules);
            rewriteOccured = (ptemp != p);
            if( !ptemp.isZero() && !ptemp.isConstant() )
            {
                GiNaCRA::BaseReductor<typename Settings::Order> reductor( gb, ptemp );
                reduced = reductor.fullReduce( );
                reductionOccured = reductor.reductionOccured( );
            }
            else
            {
                reduced = ptemp;
                reduced.setOrigins(ptemp.getOrigins());
            }
        }
    }

    Constraint_Relation relation = std::get < 1 > (it->second);
    if( rewriteOccured || reductionOccured )
    {
        assert(std::get < 0 > (it->second) != mModule->mpPassedFormula->end());
        if( reduced.isZero( ) || reduced.isConstant( ) )
        {
            bool satisfied = false;
            if( reduced.isZero( ) && !constraintRelationIsStrict( relation ) )
            {
                assert( !constraintRelationIsStrict( relation ) );
                satisfied = true;
            }
            else if( !reduced.isZero( ) )
            { // non zero
                assert( reduced.nrOfTerms( ) > 0 );
                assert( reduced.lcoeff( ) != 0 );

                const GiNaCRA::RationalNumber reducedConstant = reduced.lcoeff( );
                assert( reducedConstant != 0 );
                if( reducedConstant < 0 )
                {
                    if( relation == CR_LESS || relation == CR_LEQ || relation == CR_NEQ )
                    {
                        satisfied = true;
                    }
                }
                else
                {
                    if( relation == CR_GREATER || relation == CR_GEQ || relation == CR_NEQ )
                    {
                        satisfied = true;
                    }
                }
            }

            if( satisfied )
            {
                // remove the last formula
                mModule->removeSubformulaFromPassedFormula( std::get < 0 > (it->second) );

                std::get < 2 > (it->second).push_back( CellEntry( mBtnumber, reduced ) );
                std::set<const Formula*> originals( mModule->generateReasons( reduced.getOrigins( ).getBitVector( ) ) );

                std::get < 0 > (it->second) = mModule->mpPassedFormula->end( );
                if( Settings::addTheoryDeductions != NO_CONSTRAINTS )
                {
                    Formula* deduction = new Formula(OR);

                    for( auto jt = originals.begin(); jt != originals.end(); ++jt )
                    {
                        deduction->addSubformula( new Formula( NOT ) );
                        deduction->back()->addSubformula( (*jt)->pConstraint() );
                    }
                    deduction->addSubformula(((*(it->first))->pConstraint( )));
//                    mModule->print();
//                    std::cout << "Id="<<(*(it->first))->pConstraint()->id()<<std::endl;
//                    std::cout << "Gb learns: ";
//                    deduction->print();
 //                   std::cout << std::endl;
 //                   mModule->addDeduction(deduction);
                    #ifdef SMTRAT_DEVOPTION_Statistics
                    mStats->DeducedInequality();
                    #endif

                }
            }
            else // we have a conflict
            {

                std::set<const Formula*> infeasibleSubset( mModule->generateReasons( reduced.getOrigins( ).getBitVector( ) ) );
                infeasibleSubset.insert( *(it->first) );
                #ifdef SMTRAT_DEVOPTION_Statistics
                mStats->EffectivenessOfConflicts(infeasibleSubset.size()/mModule->mpReceivedFormula->size());
                #endif //SMTRAT_DEVOPTION_Statistics
                mModule->mInfeasibleSubsets.push_back( infeasibleSubset );
                if( Settings::withInfeasibleSubset == RETURN_DIRECTLY )
                {
                    return false;
                }
            }
        }
        else if( Settings::withInfeasibleSubset == PROCEED_ALLINEQUALITIES || mModule->mInfeasibleSubsets.empty( ) )
        {
            bool pass;
            if( Settings::passInequalities == FULL_REDUCED_IF )
            {
                pass = Settings::passPolynomial::evaluate( std::get < 2 > (it->second).front( ).second, reduced );
            }

            if( Settings::passInequalities == FULL_REDUCED || (Settings::passInequalities == FULL_REDUCED_IF && pass) )
            {
                //remove the last one
                mModule->removeSubformulaFromPassedFormula( std::get < 0 > (it->second) );
            }
            //add a new cell
            std::get < 2 > (it->second).push_back( CellEntry( mBtnumber, reduced ) );
            if( Settings::passInequalities == FULL_REDUCED || (Settings::passInequalities == FULL_REDUCED_IF && pass) )
            {
                // get the reason set for the reduced polynomial
                std::vector<std::set<const Formula*> > originals;
                originals.push_back( mModule->generateReasons( reduced.getOrigins( ).getBitVector( ) ) );
                originals.front( ).insert( *(it->first) );

                //pass the result
                //TODO: replace "Formula::constraintPool().variables()" by a smaller approximations of the variables contained in "reduced.toEx( )"
                mModule->addSubformulaToPassedFormula( new Formula( Formula::newConstraint( reduced.toEx( ), relation, Formula::constraintPool().realVariables() ) ), originals );
                //set the pointer to the passed formula accordingly.
                std::get < 0 > (it->second) = mModule->mpPassedFormula->last( );
            }
            // new constraint learning
            // If the original constraint is nonlinear
            /*if( !((*(it->first))->pConstraint( ))->isLinear() )
            {
                // We only want to learn linear constraints.
                if( reduced.isLinear() )
                {
                    // get the reason set for the reduced polynomial
                    Formula* deduction = new Formula(OR);
                    std::vector<std::set<const Formula*> > originals;
                    originals.push_back( mModule->generateReasons( reduced.getOrigins( ).getBitVector( ) ) );

                    for( auto jt =  originals.front().begin(); jt != originals.front().end(); ++jt )
                    {
                        deduction->addSubformula( new Formula( NOT ) );
                        deduction->back()->addSubformula( (*jt)->pConstraint() );
                    }

                    deduction->addSubformula( new Formula( NOT ) );
                    deduction->back()->addSubformula( (*it->first)->pConstraint() );

                    deduction->addSubformula(Formula::newConstraint( reduced.toEx( ), relation, Formula::constraintPool().realVariables() ));
                    //mModule->addDeduction(deduction);
                }
            }*/
        }
    }
    return true;
}

template<class Settings>
Answer InequalitiesTable<Settings>::reduceWRTVariableRewriteRules(const RewriteRules& rules)
{
    for( auto it = mReducedInequalities.begin( ); it != mReducedInequalities.end( ); ++it )
    {
        assert( std::get < 1 > ((*it)->second) != CR_EQ );
        if( !reduceWRTVariableRewriteRules( *it, rules ) ) {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            if( Settings::withInfeasibleSubset == RETURN_DIRECTLY )
            {
                return False;
            }
        }
    }
    if( Settings::withInfeasibleSubset != RETURN_DIRECTLY )
    {
        if( mModule->mInfeasibleSubsets.empty( ) )
        {
            return Unknown;
        }
        else
        {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            return False;
        }
    }
    else
    {
        return Unknown;
    }
}

template<class Settings>
Answer InequalitiesTable<Settings>::reduceWRTVariableRewriteRules(const  std::list< typename Rows::iterator>& ineqToBeReduced, const RewriteRules& rules )
{
    for( auto it = ineqToBeReduced.begin( ); it != ineqToBeReduced.end( ); ++it )
    {
        assert( std::get < 1 > ((*it)->second) != CR_EQ );
        if( !reduceWRTVariableRewriteRules( *it, rules ) ) {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            if( Settings::withInfeasibleSubset == RETURN_DIRECTLY )
            {
                return False;
            }
        }
    }
    if( Settings::withInfeasibleSubset != RETURN_DIRECTLY )
    {
        if( mModule->mInfeasibleSubsets.empty( ) )
        {
            return Unknown;
        }
        else
        {
            #ifdef SMTRAT_DEVOPTION_Statistics
            mStats->infeasibleInequality();
            #endif
            return False;
        }
    }
    else
    {
        return Unknown;
    }
}

template<class Settings>
bool InequalitiesTable<Settings>::reduceWRTVariableRewriteRules( typename Rows::iterator it, const RewriteRules& rules )
{
    assert(false);
    return true;
}


/**
 * Removes the row corresponding to this constraint from the inequalities table.
 * @param _formula A pointer to the constraint in the receivedFormula which has to be removed.
 */

template<class Settings>
void InequalitiesTable<Settings>::removeInequality( Formula::const_iterator _formula )
{
    mReducedInequalities.erase( _formula );
    if( mNewConstraints != mReducedInequalities.end( ) && _formula == mNewConstraints->first )
    {
        ++mNewConstraints;
    }
}

/**
 * A print function for the inequalitiestable
 * @param os
 */

template<class Settings>
void InequalitiesTable<Settings>::print( std::ostream& os ) const
{
    std::cout << "Bt: " << mBtnumber << std::endl;
    for( auto it = mReducedInequalities.begin( ); it != mReducedInequalities.end( ); ++it )
    {
        typename std::list<CellEntry>::const_iterator listEnd = std::get < 2 > (it->second).end( );
        std::cout << *(it->first) << " -> " << *(std::get < 0 > (it->second)) << std::endl;
        for(typename std::list<CellEntry>::const_iterator jt = std::get < 2 > (it->second).begin( ); jt != listEnd; ++jt )
        {
            std::cout << "\t(" << jt->first << ") " << jt->second << " [";
            jt->second.getOrigins().print();
            std::cout << "] " << std::endl;

        }
    }
}

} // namespace smtrat





