=> Request Management System

This is a multithreaded program written in C++ that simulates a request management system with worker threads, services, and requests. 
The program creates multiple worker threads that serve requests from different services. 
Each worker thread has a certain priority level and a limited number of resources available for serving requests.

Requests arrive at different services, and each request requires a certain amount of resources to be processed. 
The program checks the availability of resources in the worker threads and assigns requests to them according to the priority of the worker thread. 
If there are not enough resources available in any of the worker threads, the request is blocked until sufficient resources become available. 
If the request cannot be processed due to a lack of resources in any of the worker threads, it is dropped and the count is incremented for the same.

The program records the arrival time, start time, and end time of each request, as well as the waiting time and turnaround time of each request. 
The waiting time is the time elapsed between the arrival of the request and the start of its execution. 
The turnaround time is the time elapsed between the arrival of the request and its completion.

The program uses mutexes and condition variables to ensure thread safety and prevent race conditions. 
It also uses the STL containers and algorithms for efficient data management.

=> Requirements:
C++11 or higher
pthread library
chrono library

=> How to Use
The repository contains the makefile in it. Just run the make file.
There is one Input.txt file which is the input file for the code.

If only make command is executed then it will run the code with the input files input.

If you want to give input from terminal then run "make run2" command.

The program will then simulate the request management system, and the output will be displayed on the console.

=> Authors
[Harsh verma - 224101021]
   IIT Guwahati

NOTE: The makefile is created in the macOS so it might not work directly for the ubuntu system. Add -lpthread flag to make it compatible to ubuntu system