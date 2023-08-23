#include <iostream>
#include <stdio.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <vector>
#include <queue>
#include <stdlib.h>
#include <chrono>

using namespace std;



// Structure of the worker thread
struct WorkerThread {
    int priority;
    int resources;
    int leftResources;
    pthread_mutex_t workerMutex;
    bool stopServing;
};

typedef struct WorkerThread workerThread;

// Structure of the Request containing necessary data members
struct Request {
    int id;
    int type;
    int resourcesRequired;
    std::chrono::high_resolution_clock::time_point arrivalTime;
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point endTime;
    std::chrono::milliseconds waitingTime;
    std::chrono::milliseconds taTime;
};

typedef struct Request request;

// Structure of Service with necessary data members
struct Service {
    pthread_mutex_t srvMutex;
    int type;
    int numThreads;
    vector<workerThread*> workerThreads;
    bool stopService;
};
typedef struct Service service;

// ------------------------- Global and shared variables -------------------------
vector<queue<request*>> requestQueue; // Global Request queue to pass request to specific system
int requestsDroppedDueToResourceConstraints = 0; // Count to store the dropped request
mutex droppedCount; // Mutex for the above

int requestsBlockedDueToResourceConstraints = 0; // Count to store the blocked requests
mutex blockedCount; // Mutex for the same

// Compare function to sort the worker threads based on priority
const bool compare(workerThread* w1, workerThread* w2) {
    return w1->priority < w2->priority;
}

// Compare function to sort the requests based on type and start time
const bool newCompare(request* r1, request* r2) {
    // if (r1->type == r2->type) {
        return r1->startTime < r2->startTime;
    // }
    // return r1->type < r2->type;
}


/**
 * @brief The execute function to execute the specific request
 * 
 * @param sType - The service type
 * @param r - Request pointer
 * @param w - Worker performing the request pointer
 */
void executeRequest(int sType, request* r, workerThread* w) {

    // Update the start time of the request
    r->startTime = std::chrono::high_resolution_clock::now();
    // printf("[Execution Started] Request: %d\n", r->id);

    usleep(100000); // Sleeping for 100 ms as the burst time of the request is considered as the static 100 ms
    // printf("[Execution Completed] Request: %d\n", r->id);
    // Update the end time of the request
    r->endTime = std::chrono::high_resolution_clock::now();
    
    // Add the resources back to the pool
    pthread_mutex_lock(&w->workerMutex);
    w->leftResources += r->resourcesRequired;
    pthread_mutex_unlock(&w->workerMutex);

    // Calculate the waitning time and turn around time of the requests
    r->waitingTime = std::chrono::duration_cast<std::chrono::milliseconds>(r->startTime - r->arrivalTime);
    r->taTime = std::chrono::duration_cast<std::chrono::milliseconds>(r->endTime - r->arrivalTime);
    
}

/**
 * The worker function that would check the assigned request and execute it simultaneously untill resources available
*/
void workerFunction(int sType, workerThread* w, queue<request*>& q, queue<request*>& q1) {
    vector<thread> requestThreads; // The request threads
    bool stopFlag = false; // Stop the service flag
    
    while (true) {
        // Checking if to stop the service or not
        pthread_mutex_lock(&w->workerMutex);
        stopFlag = w->stopServing;
        pthread_mutex_unlock(&w->workerMutex);

        // Checking the primary queue
        if (!q.empty()) {
            request* r = q.front();
            q.pop();

            requestThreads.emplace_back(executeRequest, sType, r, w);

        } else if (!q1.empty()) { // Checking the secondary queue for blocked requests
            request* r = q1.front();
            pthread_mutex_lock(&w->workerMutex);
            // Checking if those requests can be performed now or not
            if (w->leftResources >= r->resourcesRequired) {
                q1.pop();
                requestThreads.emplace_back(executeRequest, sType, r, w);
            }
            pthread_mutex_unlock(&w->workerMutex);
        } else if (stopFlag) { // If both queue are empty and the signal is to stop the service then stop
            break;
        }

    }

    // Wait for child threads to finish
    for (auto& t: requestThreads) {
        t.join();
    }
}


/**
 * The service function that would be responsible for creating the worker threads
 * Checking the queue for any new requests coming and 
 * assigning it to the worker based on priority and resource availability
*/
void serviceFunction(service* srv) {

    vector<queue<request*>> q(srv->numThreads); // Primary queue for each worker thread
    vector<queue<request*>> q1(srv->numThreads);  // Secondary queue for each worker thread

    thread workers[srv->numThreads]; // Worker threads

    // Sorting them based on priority
    sort(srv->workerThreads.begin(), srv->workerThreads.end(), compare);

    // Creating number of thread workers
    for (int i = 0; i < srv->numThreads; i++) {
        workers[i] = thread(workerFunction, srv->type, srv->workerThreads[i], std::ref(q[i]), std::ref(q1[i]));
    }

    while (true)
    {
        bool stopFlag = false; // Checking stop signal

        pthread_mutex_lock(&srv->srvMutex); // Acquiring the necessary mutex
        stopFlag = srv->stopService;

        // Checking the queue
        if (!requestQueue[srv->type].empty()) {
            
            // Pop the request from the queue
            request* r = requestQueue[srv->type].front();
            requestQueue[srv->type].pop();


            int pos = 0;
            bool flag = false;
            int dropFlag = 0;

            // Scanning the resources in the workers thread
            for (int w = 0; w < srv->numThreads; w++) {

                pthread_mutex_lock(&srv->workerThreads[w]->workerMutex);

                // Assigning to primary Queue
                if (r->resourcesRequired <= srv->workerThreads[w]->leftResources) {
                    srv->workerThreads[w]->leftResources -= r->resourcesRequired;
                    q[w].push(r);
                    pthread_mutex_unlock(&srv->workerThreads[w]->workerMutex);
                    break;
                } else if (r->resourcesRequired <= srv->workerThreads[w]->resources) { // Assigning to secondary queue
                    if (!flag) {
                        flag = true;
                        pos = w;
                    }
                } else { // Checking if it needs to be dropped or not
                    dropFlag++;
                }
                pthread_mutex_unlock(&srv->workerThreads[w]->workerMutex);
            }

            // Checking to update the counts
            if (flag) {
                q1[pos].push(r);
                blockedCount.lock();
                requestsBlockedDueToResourceConstraints++;
                blockedCount.unlock();
            }

            // Updating drop count
            if (dropFlag == srv->numThreads) {
                // printf("[Dropped Due to Resource Constraints] Request %d is dropped because no worker has enough resources for it.\n", r->id);
                droppedCount.lock();
                requestsDroppedDueToResourceConstraints++;
                droppedCount.unlock();
            }

        } else if (stopFlag) { // Checking the stop signal
            pthread_mutex_unlock(&srv->srvMutex);
            break;
        }
        pthread_mutex_unlock(&srv->srvMutex);

    }
    
    // Signalling the child worker threads that there are no more jobs for them
    for (workerThread* w: srv->workerThreads) {
        pthread_mutex_lock(&w->workerMutex);
        w->stopServing = true;
        pthread_mutex_unlock(&w->workerMutex);
    }
    
    // Waiting for the threads to complete their execution
    for (int i = 0; i < srv->numThreads; i++) {
        workers[i].join();
    }
    
}

/**
 * Main function
*/
int main(int argc, char const *argv[])
{
    srand(time(nullptr));

    cout << "\n\n--------------------------------------- Inputs ----------------------------------------\n";

    int numberOfServices;
    cout << "\n=> Enter number of services: ";
    cin >> numberOfServices;
    int numberOfWorkersInEachService;
    cout << "\n=> Enter the number of worker threads for each service: ";
    cin >> numberOfWorkersInEachService;

    thread serviceThreads[numberOfServices];
    vector<service*> services;
    requestQueue.resize(numberOfServices);

    int maxResource = 0;
    

    for (int i = 0; i < numberOfServices; i++) {

        // Creating the services
        service* s = (service*) malloc(sizeof(service));
        s->type = i;
        s->numThreads = numberOfWorkersInEachService;
        s->stopService = false;

        printf("\n=> Enter the priority and resources for worker threads in service %d below: \n", i);
        // Taking input the priority and resources for each worker threads
        for (int j = 0; j < numberOfWorkersInEachService; j++) {
            int wPriority, wResource;
            cin >> wPriority >> wResource;

            maxResource = max(maxResource, wResource);

            workerThread* t1 = (workerThread*) malloc (sizeof (workerThread));
            t1->priority = wPriority;
            t1->stopServing = false;
            t1->leftResources = t1->resources = wResource;
            s->workerThreads.push_back(t1);
        }

        services.push_back(s);
        serviceThreads[i] = thread(serviceFunction, services[i]);
    }

    maxResource += 5;
    int minResources = 2;

    vector<request*> requests;

    int numOfRequests;
    cout << "Enter the number of requests: ";
    cin >> numOfRequests;


    // Generating the random requests
    for (int x = 0; x < numOfRequests; x++) {
        request* r = (request*) malloc(sizeof(request));
        r->type = rand() % numberOfServices;
        r->id = x;
        r->resourcesRequired = rand() % (maxResource - minResources + 1) + minResources;
        r->arrivalTime = std::chrono::high_resolution_clock::now();
        requests.push_back(r);
        pthread_mutex_lock(&services[r->type]->srvMutex);
        requestQueue[r->type].push(requests[x]);
        pthread_mutex_unlock(&services[r->type]->srvMutex);
    }

    // Signalling the service threads that the requests has been stopped coming from the main thread
    for (service* s: services) {
        pthread_mutex_lock(&s->srvMutex);
        s->stopService = true;
        pthread_mutex_unlock(&s->srvMutex);
    }

    // Waiting for all the services to exit
    for (int i = 0; i < numberOfServices; i++) {
        serviceThreads[i].join();
    }

    sort(requests.begin(), requests.end(), newCompare);

    printf("\n\n==================================== Results Summary ====================================\n\n");
    printf("                          Requests Dropped due to resource constraints\n\n");
    printf(" Request #  |  Service Type  |  Resource Requirement \n");
    printf("---------------------------------------------------- \n");
    int k = 0;



    while (k < requestsDroppedDueToResourceConstraints) {
        printf(" %9d  |  %12d  |  %21d \n", requests[k]->id, requests[k]->type, requests[k]->resourcesRequired);
        k++;
    }

    long long avgWaitingTime = 0;
    long long avgTurnAroundTime = 0;

    printf("\n\n------------------------- Process order of requests -------------------------\n\n");
    printf(" Request #  |  Type  |  Resources  |     Arrival time    |     Start time     |       End Time     |  Wait Time  |  Turnaround Time\n");
    printf("-----------------------------------------------------------------------------------------------------------------------------------\n");

    while (k < numOfRequests) {
        avgWaitingTime += requests[k]->waitingTime.count();
        avgTurnAroundTime += requests[k]->taTime.count();

        printf(" %9d  |  %4d  |  %9d  |  %17lld  |  %15lld  |  %15lld  |  %9lld  |  %15lld\n", 
                requests[k]->id, requests[k]->type, requests[k]->resourcesRequired, 
                std::chrono::time_point_cast<std::chrono::milliseconds>(requests[k]->arrivalTime).time_since_epoch().count(), 
                std::chrono::time_point_cast<std::chrono::milliseconds>(requests[k]->startTime).time_since_epoch().count(), 
                std::chrono::time_point_cast<std::chrono::milliseconds>(requests[k]->endTime).time_since_epoch().count(),
                requests[k]->waitingTime.count(), requests[k]->taTime.count());
        k++;
    }

    avgWaitingTime /= (numOfRequests - requestsDroppedDueToResourceConstraints);
    avgTurnAroundTime /= (numOfRequests - requestsDroppedDueToResourceConstraints);

    printf("-----------------------------------------------------------------------------------------------------------------------------------\n\n\n\n");

    cout << "=> Average Waiting Time = " << avgWaitingTime << endl;
    cout << "=> Average Turn Around Time = " << avgTurnAroundTime << endl;
    cout << "=> Number of requests rejected due to lack of resources = " << requestsDroppedDueToResourceConstraints << endl;
    cout << "=> Number of requests blocked due to lack of resources = " << requestsBlockedDueToResourceConstraints << endl << endl;

    return 0;
}
