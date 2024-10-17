# Multithreaded Rental Service
This project implements a multi-threaded system designed to optimize the profit of a rental company using an auction-like system for renting tools. The system processes problem packs from various companies concurrently, utilizing worker threads for solving problems efficiently.

## Overview
The system consists of several key classes that facilitate communication between companies, manage problem-solving tasks, and handle synchronization across threads. 
The main components of the project are:
- **ProblemPack**: Wraps a collection of problems associated with a specific company.
- **Problem**: Represents an individual problem contained within a `ProblemPack`.
- **CommunicationBuffer**: Manages the communication between companies and worker threads, allowing the transfer of problem packs.
- **Company**: Represents a company that provides problems for solving and handles solved problem packs. Ensures that problems are sent back in the order they initially came.
- **Solver**: Manages the problem-solving process, ensuring that problems are processed, and that companies are notified of completed tasks.
- **COptimizer**: The main orchestrator that initializes companies, starts worker threads, and manages the overall process.

## Class Descriptions

### ProblemPack
- **Constructors**:
  - `ProblemPack(AProblemPack pack, size_t companyIndex)`: Initializes a new ProblemPack with a given problem set and the index of the company it originated from.
- **Methods**:
  - `void problemSolved()`: Decrements the count of remaining problems.
  - `bool isSolved() const`: Checks if all problems in the pack are solved.
  - `AProblemPack& getInnerProblemPack()`: Returns the inner problem pack.
  - `size_t getCompanyIndex() const`: Retrieves the company index.

### Problem
- **Constructors**:
  - `Problem(AProblem problem, shared_ptr<ProblemPack> pack)`: Wraps an individual problem and associates it with its originating `ProblemPack`.
- **Methods**:
  - `AProblem& getInnerProblem()`: Returns the inner problem.
  - `shared_ptr<ProblemPack>& getOriginatingPack()`: Returns a reference to the originating pack.

### CommunicationBuffer
- **Methods**:
  - `void addAndNotify(shared_ptr<ProblemPack>& newPack)`: Adds a new problem pack to the queue and notifies worker threads if the queue was previously empty.
  - `Problem getProblem()`: Retrieves and removes the front problem from the queue.
  - `void waitForPacksOrTermination(atomic_int& activeCompanies, unique_lock<mutex>& lock)`: Waits for new problem packs or termination state signals.

### Company
- **Constructors**:
  - `Company(ACompany company, size_t companyIndex)`: Initializes a company with its corresponding index.
- **Methods**:
  - `void initializeCommunicationThreads(CommunicationBuffer& communicationBuffer, atomic_int& activeCompanies, vector<Company>& companies)`: Starts the threads for receiving new packs and sending solved packs.
  - `void joinCommunicationThreads()`: Joins the communication threads.

### Solver
- **Constructors**:
  - `Solver()`: Initializes the solver component by a factory function.
- **Methods**:
  - `void addProblem(Problem problem)`: Adds a problem to the solver.
  - `void solve()`: Processes all problems currently queued in the solver.
  - `void notifyCompanies(vector<Company>& companies)`: Notifies companies when their problems are solved. Also clears the inner queue of problems in the process.
 
### COptimizer
  - `void start(int threadCount)`: Initializes and starts worker threads and communication threads for companies.
  - `void stop()`: Joins all worker and communication threads to terminate processing.
  - `void addCompany(ACompany company)`: Adds a new company to the optimizer for processing.

## Further Discovery
The code itself is quite intuitive and has lots of comments, describing it's functionality, so feel free to check the implementation details:)




































