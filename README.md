### C++ Threading Utilities
[![Build Status](https://travis-ci.org/Grauniad/CPPThreadUtils.svg?branch=master)](https://travis-ci.org/Grauniad/CPPThreadUtils)
[![Coverage Status](https://coveralls.io/repos/github/Grauniad/CPPThreadUtils/badge.svg?branch=master)](https://coveralls.io/github/Grauniad/CPPThreadUtils?branch=master)

High-level utilities for working with threads in modern C++.

Utility         | See Header       | Description
-------         | ----------       | -------------
IPostable       | IPostable.h      | Trivially interface for event_loops permitting work to be posted to them
WorkerThread    | WorkerTrhead.h   | Spawn a child thread which will yield until work is posted to it. (Implements IPostable)
PipePublisher   | PipePublisher.h  | Publisher which supports multiple clients registering as update sinks.
PipeSubscriber  | PipeSubscriber.h | Single-Producer, Single-Consumer implementation of the publisher client.

This project used to be maintained as part of a private "dev_tools" monolith: https://github.com/Grauniad/dev_tools_legacy/tree/master/CPP/Libraries/libThreadComms 
