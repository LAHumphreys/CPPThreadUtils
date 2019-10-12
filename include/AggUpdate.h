//
// Created by lukeh on 12/10/2019.
//

#ifndef THREADCOMMS_AGGUPDATE_H
#define THREADCOMMS_AGGUPDATE_H
#include <memory>

enum class AggUpdateType: char {
    NEW ='N',
    UPDATE ='U',
    NONE ='O',
};

std::ostream& operator<<(std::ostream& os, const AggUpdateType& up) {
    switch (up) {
        case AggUpdateType::NEW:
            os << "AggUpdateType::NEW";
            break;
        case AggUpdateType::UPDATE:
            os << "AggUpdateType::UPDATE";
            break;
        default:
            os << "AggUpdateType::UNKNOWN";
            break;
    }
    return os;
}

template<class T>
struct AggUpdate {
    std::shared_ptr<const T> data;
    AggUpdateType            updateType;
};

#endif //THREADCOMMS_AGGUPDATE_H
