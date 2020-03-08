//
// Created by lhumphreys on 08/03/2020.
//
#include <AggUpdate.h>
#include <ostream>

std::ostream& operator<<(std::ostream& os, const AggUpdateType& up)
{
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

