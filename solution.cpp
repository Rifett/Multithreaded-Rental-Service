#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"
using namespace std;
#endif /* __PROGTEST__ */


//============================================PROBLEM PACK WRAPPER CLASS===========================
class ProblemPack
{
public:
    ProblemPack( AProblemPack pack, size_t companyIndex ) : innerProblemPack( std::move( pack ) ), companyIndex( companyIndex ), leftToSolve( innerProblemPack->m_Problems.size() ) { };

    void problemSolved( void ) { --leftToSolve; }

    bool isSolved( void ) const { return !leftToSolve; }

    AProblemPack& getInnerProblemPack( void ) { return innerProblemPack; }

    size_t getCompanyIndex( void ) const { return companyIndex; }

    explicit operator bool( void ) const { return innerProblemPack.operator bool(); }

private:
    AProblemPack innerProblemPack;
    size_t companyIndex; // Index of a company where the problem pack came from
    atomic_size_t leftToSolve;
};


//============================================PROBLEM WRAPPER CLASS================================
class Problem
{
public:
    Problem(AProblem problem, shared_ptr<ProblemPack> pack) : innerProblem( std::move( problem ) ), originatingPack( std::move( pack ) ) {}

    AProblem& getInnerProblem() {
        return innerProblem;
    }

    shared_ptr<ProblemPack>& getOriginatingPack() {
        return originatingPack;
    }

private:
    AProblem innerProblem;
    shared_ptr<ProblemPack> originatingPack;
};


//============================================COMMUNICATION BUFFER=================================
class CommunicationBuffer
{
public:
    // Default constructor -> initializes assigns mutex to the inner lock and unlocks it
    CommunicationBuffer() : innerLock( innerMutex ) { innerLock.unlock(); }

    // Adds new problem pack into the queue and notifies one waiting worker thread in case queue was empty
    void addAndNotify( shared_ptr<ProblemPack>& newPack ) {
        bool bufferWasEmpty = this->empty();

        for ( auto& problem: newPack->getInnerProblemPack()->m_Problems )
            innerQueue.emplace( problem, newPack );

        if ( bufferWasEmpty )
            innerConditionVariable.notify_one();
    }

    void notifyAllWorkers( void ) { innerConditionVariable.notify_all(); }

    void notifyOneWorker( void ) { innerConditionVariable.notify_one(); }

    void lock( void ) { innerLock.lock(); }

    void unlock( void ) { innerLock.unlock(); }

    bool empty( void ) { return innerQueue.empty(); }

    Problem getProblem( void ) { return innerQueue.front(); }

    void pop( void ) { innerQueue.pop(); }

    // Waits for packs to be added into the buffer or for the termination state
    void waitForPacksOrTermination( atomic_int& activeCompanies ) {
        innerConditionVariable.wait(innerLock, [&] {
            return !this->empty() || ( !activeCompanies && this->empty() );
        });
    }

private:
    queue<Problem> innerQueue;
    mutex innerMutex;
    unique_lock<mutex> innerLock;
    condition_variable innerConditionVariable;
};


//============================================COMPANY WRAPPER CLASS================================
class Company
{
public:
    Company(ACompany company, size_t companyIndex) : innerCompany( std::move( company ) ), companyIndex( companyIndex ), innerLock( innerMutex ) { innerLock.unlock(); };

    Company(const Company &company) : innerCompany( company.innerCompany ), companyIndex( company.companyIndex ) { };

    void initializeCommunicationThreads( CommunicationBuffer& communicationBuffer, atomic_int& activeCompanies, vector<Company>& companies ) {
        newPacksReceiver = thread( &Company::getNewPacks, this, std::ref( communicationBuffer ), std::ref( activeCompanies ), std::ref( companies ) );
        solvedPacksSender = thread( &Company::sendSolvedPacks, this, std::ref( activeCompanies ) );
    }

    void joinCommunicationThreads( void ) {
        newPacksReceiver.join();
        solvedPacksSender.join();
    }

    void notify( void ) { innerConditionVariable.notify_one(); }


private:
    void getNewPacks( CommunicationBuffer& communicationBuffer, atomic_int& activeCompanies, vector<Company>& companies ) {
        auto newPack = make_shared<ProblemPack>( innerCompany->waitForPack(), companyIndex );

        // While there is a pack
        while ( newPack.get() ) {
            // Add it to the inner queue to preserve the ordering
            innerLock.lock();
            innerQueue.push( newPack );
            innerLock.unlock();

            // And add it to the communication buffer to transfer pack to worker threads
            communicationBuffer.lock();
            communicationBuffer.addAndNotify( newPack );
            communicationBuffer.unlock();

            // Get new pack
            newPack = make_shared<ProblemPack>(innerCompany->waitForPack(), companyIndex);
        }

        // All packs are out -> terminate the thread
        activeCompanies--;
        if ( !activeCompanies ) {
            communicationBuffer.notifyAllWorkers();
            notifyAllCompanies( companies );
        }
    }

    void sendSolvedPacks( atomic_int& activeCompanies ) {
        innerLock.lock();

        while ( true ) {
            // Wait for the front problem pack to be solved or for the termination state
            waitForSolvedPacksOrTermination( activeCompanies );

            // Termination stage check
            if ( !activeCompanies && innerQueue.empty() )
                break;

            // Front problem pack is solved -> send it back to the company
            sendBackFrontPack();
        }

        // Termination stage (just for the sake of perfectionism)
        innerLock.unlock();
    }

    void notifyAllCompanies( vector<Company>& companies ) {
        for ( auto& company: companies)
            company.notify();
    }

    void waitForSolvedPacksOrTermination( atomic_int& activeCompanies ) {
        innerConditionVariable.wait(innerLock, [&] {
            return (!innerQueue.empty() && innerQueue.front()->isSolved()) || (!activeCompanies && innerQueue.empty());
        });
    }

    void sendBackFrontPack( void ) {
        // Get solved pack from the queue
        auto solvedPack = innerQueue.front();
        innerQueue.pop();

        // Open inner queue for newPacksReceiver thread
        innerLock.unlock();

        // Send back solved pack
        innerCompany->solvedPack( solvedPack->getInnerProblemPack() );

        // Lock back the mutex
        innerLock.lock();
    }


    ACompany innerCompany;
    size_t companyIndex;
    queue<shared_ptr<ProblemPack>> innerQueue;
    mutex innerMutex;
    unique_lock<mutex> innerLock;
    condition_variable innerConditionVariable;
    thread newPacksReceiver;
    thread solvedPacksSender;
};


//============================================SOLVER WRAPPER CLASS=================================
class Solver
{
public:
    Solver() : innerSolver( createProgtestSolver() ) {}

    void addProblem( Problem problem ) {
        innerSolver->addProblem( problem.getInnerProblem() );
        problemsQueue.push( problem );
    }

    bool full( void ) { return !innerSolver->hasFreeCapacity(); }

    bool empty( void ) { return problemsQueue.empty(); }

    void solve( void ) { innerSolver->solve(); }

    void notifyCompanies( vector<Company>& companies ) {
        while ( !problemsQueue.empty() ) {
            // Mark solved problem
            problemsQueue.front().getOriginatingPack()->problemSolved();

            // Notify company to check the pack
            auto companyIndex = problemsQueue.front().getOriginatingPack()->getCompanyIndex();
            companies.at( companyIndex ).notify();

            // Remove problem from the queue
            problemsQueue.pop();
        }
    }

private:
    AProgtestSolver innerSolver;
    queue<Problem> problemsQueue;
};


//============================================C OPTIMIZER CLASS====================================
class COptimizer 
{
public:
    static bool usingProgtestSolver ( void )
    {
        return true;
    }

    static void checkAlgorithm ( AProblem problem )
    {
        // dummy implementation if usingProgtestSolver() returns true
    }

    void start ( int threadCount )
    {
        // Initialize worker threads
        this->initializeWorkerThreads( threadCount );

        // Initialize companies' communication threads
        for ( auto& company: companies )
            company.initializeCommunicationThreads( communicationBuffer, activeCompanies, companies );
    }

    void stop ( void )
    {
        // Join worker threads
        this->joinWorkerThreads();

        // Join companies' communication threads
        for ( auto& company: companies )
            company.joinCommunicationThreads();
    }

    void addCompany ( ACompany company )
    {
        companies.emplace_back(std::move( company ), companies.size() );
    }

private:
    void computeAndNotify( void ) {
        communicationBuffer.lock();

        while ( true ) {
            // Wait for some problems to appear in the buffer or for the termination state
            communicationBuffer.waitForPacksOrTermination( activeCompanies );

            // Termination state check
            if ( !activeCompanies && communicationBuffer.empty() )
                break;

            // Some problems are in the buffer -> take them and put into solver
            fillTheSolver();

            // Solver is full
            if ( solver.full() ) {
                // Clear the solver -> solver is empty and ready for further work and fullSolver has all the collected problems
                Solver fullSolver;
                std::swap(solver, fullSolver);

                // Open the critical region for other worker threads
                communicationBuffer.unlock();

                // Notify one worker thread and do the computation
                communicationBuffer.notifyOneWorker();
                fullSolver.solve();

                // Notify the companies, problem packs of which are solved
                fullSolver.notifyCompanies( companies );

                // Lock back the critical region
                communicationBuffer.lock();
            }
        }


        // Termination stage
        // Solve all the remaining problems
        if ( !solver.empty() ) {
            solver.solve();
            solver.notifyCompanies( companies );
        }

        // Open the critical region
        communicationBuffer.unlock();
    }

    void fillTheSolver( void ) {
        while ( !solver.full() && !communicationBuffer.empty() ) {
            solver.addProblem( communicationBuffer.getProblem() );
        }
    }

    void initializeWorkerThreads( int threadCount )
    {
        for ( auto& workerThread: workerThreads ) {
            workerThread = thread( &COptimizer::computeAndNotify, this );
        }
    }

    void joinWorkerThreads( void )
    {
        for ( auto& workerThread: workerThreads )
            workerThread.join();
    }


    Solver solver;
    atomic_int activeCompanies;
    CommunicationBuffer communicationBuffer;
    vector<thread> workerThreads;
    vector<Company> companies;
};


#ifndef __PROGTEST__
int                                    main                                    ( void )
{
  COptimizer optimizer;
  ACompanyTest  company = std::make_shared<CCompanyTest> ();
  optimizer . addCompany ( company );
  optimizer . start ( 4 );
  optimizer . stop  ();
  if ( ! company -> allProcessed () )
    throw std::logic_error ( "(some) problems were not correctly processed" );
  return 0;  
}
#endif /* __PROGTEST__ */ 



























