//
// Created by lukeh on 12/10/2019.
//

#ifndef THREADCOMMS_AGGUPDATE_H
#define THREADCOMMS_AGGUPDATE_H
#include <memory>

template<class T>
struct AggUpdate {
    std::shared_ptr<T> data;
};

#endif //THREADCOMMS_AGGUPDATE_H
