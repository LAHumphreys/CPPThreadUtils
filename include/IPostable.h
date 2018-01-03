/*
 * Interface to be implemented by classes which support having 
 * tasks posted to them
 *
 *  Created on: 22nd December 2015
 *      Author: lhumphreys
 */

#ifndef DEV_TOOLS_CPP_LIBRARIES_LIB_THEAD_COMMS_INTERFACE_POSTABLE_H__
#define DEV_TOOLS_CPP_LIBRARIES_LIB_THEAD_COMMS_INTERFACE_POSTABLE_H__
#include <functional>

/**
 * By implementing this interface, a class declares that it
 * can have tasks posted to it, which will be run on its "event loop"
 */
class IPostable {
public:
    typedef std::function<void()> Task;

    /**
     * Post a task to the event loop.
     *
     * @param t  The task to be run.
     */
    virtual void PostTask(const Task& t) = 0;

    virtual ~IPostable() {}
};

#endif
