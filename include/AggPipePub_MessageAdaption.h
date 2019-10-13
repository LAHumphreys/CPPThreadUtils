//
// Created by lukeh on 13/10/2019.
//

#ifndef THREADCOMMS_AGGPIPEPUB_MESSAGEADAPTION_H
#define THREADCOMMS_AGGPIPEPUB_MESSAGEADAPTION_H

namespace AggPipePub_Adaption {

    /*****************************************
     *    Does Message have a GetAggId()?
     *****************************************/
    template<class Message, bool result = std::is_member_function_pointer<decltype(&Message::GetAggId)>::value>
    constexpr bool HasGetAggId(int) { return result; }

    template<class Message>
    constexpr bool HasGetAggId(...) { return false; }

    /*****************************************
     *    Does Message have a id member?
     *****************************************/
    template<class Message, bool result = std::is_member_object_pointer<decltype(&Message::id)>::value>
    constexpr bool HasId(int) { return result; }

    template<class Message>
    constexpr bool HasId(...) { return false; }

    /*****************************************************
     *    Does the Message provide an IsAggEqual method?
     *****************************************************/
    template<class Message, bool result = std::is_member_function_pointer<decltype(&Message::IsAggEqual)>::value>
    constexpr bool HasAggEqual(int) { return result; }

    template<class Message>
    constexpr bool HasAggEqual(...) { return false; }

    /******************************************************
     *    Provide ID Access to a Message that has a valid
     *    GetAggId method.
     *
     *    NOTE: We still be a valid object if it doesn't
     *          to ensure both types of enable_if are
     *          valid - HOWEVER, we don't require working
     *          getters / types in that scenario
     ******************************************************/
    template<class Message, bool HasGetter>
    struct GetIdAdp {
        using GetAggIdResultType =
        typename std::conditional<
                HasGetter,
                decltype(((Message*)(nullptr))->GetAggId()),
                nullptr_t>::type;
        using GetAggIdValueType =
                typename std::remove_reference<GetAggIdResultType>::type;

        using EnableIf_HasGetAggId =
            typename std::enable_if<HasGetter, GetAggIdValueType>::type;

        static constexpr EnableIf_HasGetAggId Get(const Message& m) {
            return m.GetAggId();
        }
    };

    /******************************************************
     *    Provide ID Access to a Message that has a valid
     *    id member variable
     *
     *    NOTE: We still be a valid object if it doesn't
     *          to ensure both types of enable_if are
     *          valid - HOWEVER, we don't require working
     *          getters / types in that scenario
     ******************************************************/
    template<class Message, bool HasId>
    struct IdAdp {
        using GetAggIdResultType =
        typename std::conditional<
                HasId,
                decltype(((Message*)(nullptr))->id),
                nullptr_t>::type;
        using GetAggIdValueType  = typename std::remove_reference<GetAggIdResultType>::type;

        using EnableIf_HasId =
        typename std::enable_if<HasId, GetAggIdValueType>::type;

        static constexpr EnableIf_HasId Get(const Message& m) {
            return m.id;
        }
    };

    /******************************************************
     *  Adapt the message type to provide access to the Id
     *  field.
     ******************************************************/
    template<class Message>
    struct MessageAdapter {
        static constexpr bool hasGetAggId = HasGetAggId<Message>(0);
        static constexpr bool hasId = HasId<Message>(0);
        static constexpr bool hasAggEq = HasAggEqual<Message>(0);

        // Ensure the message as at least one way of accessing the id type
        static_assert(hasGetAggId || hasId,
                       "Message object has no viable Id Type!"
                       " Object must provide one of :\n"
                       "   struct Message { \n"
                       "        <id type> id;\n"
                       "        <id type> GetAggId() const;"
                       "   };");



        using GetAdapter =
            typename std::conditional<
                         hasGetAggId,
                         GetIdAdp<Message, hasGetAggId>,
                         IdAdp<Message, hasId>>::type;

        using GetAggIdValueType  = typename GetAdapter::GetAggIdValueType;

        static constexpr GetAggIdValueType Get(const Message& m) {
            return GetAdapter::Get(m);
        }

        using EnableIf_HasAggEq =
           typename std::enable_if<hasAggEq, bool>::type;

        static constexpr EnableIf_HasAggEq IsEqual(
                const Message& orig, const Message& upd)
        {
            return orig.IsAggEqual(upd);
        }
    };
}


#endif //THREADCOMMS_AGGPIPEPUB_MESSAGEADAPTION_H
