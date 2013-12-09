/*
Copyright (c) 2013, David C Horton

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
//
#include <iostream>

#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

#include "client.hpp"
#include "controller.hpp"

using namespace json_spirit ;

namespace drachtio {
    std::size_t hash_value( Client const &c ) {
        std::size_t seed = 0 ;
        boost::hash_combine( seed, c.const_socket().local_endpoint().address().to_string()  ) ;
        boost::hash_combine( seed, c.const_socket().local_endpoint().port() ) ;
        return seed ;
    }

	Client::Client( boost::asio::io_service& io_service, ClientController& controller ) : m_sock(io_service), m_controller( controller ),  
        m_state(initial), m_buffer(8192), m_nMessageLength(0) {
    }
    Client::~Client() {
    }

    void Client::start() {

        DR_LOG(log_info) << "Received connection from client at " << m_sock.remote_endpoint().address().to_string() << ":" << m_sock.remote_endpoint().port() << endl ;

        /* wait for authentication challenge */
        m_controller.join( shared_from_this() ) ;
        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
                        boost::bind( &Client::read_handler, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred ) ) ;
        
    }
    void Client::read_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) {

        if( ec ) {
            DR_LOG(log_error) << ec << endl ;
            m_controller.leave( shared_from_this() ) ;
            return ;
        }
 
        /*  add the data to any partial message we have, then check if we have received the expeected length of data */
        if( m_nMessageLength > 0 ) {
            for( unsigned int i = 0; i < bytes_transferred; i++ ) m_buffer.push_back( m_readBuf[i] ) ;
        }
        else {
            /* read the length specifier at the front, skip the delimiting hash, add the rest */
            boost::array<char, 5> ch ;
            memset( ch.data(), 0, sizeof(ch)) ;
            unsigned int i = 0 ;
            for( ; m_readBuf[i] != '#' && i < bytes_transferred; i++ ) {
                ch[i] = m_readBuf[i] ;
            }
            m_nMessageLength = boost::lexical_cast<unsigned int>(ch.data()); 

            if( i < bytes_transferred ) {
                for( i++; i < bytes_transferred; i++ ) m_buffer.push_back( m_readBuf[i] ) ;
            }
        }

        /* now check if we have a full message to process */
        if( m_buffer.size() == m_nMessageLength ) {
  
            JsonMsg msgResponse ;
            bool bDisconnect = false ;
            try {
 
                boost::shared_ptr<JsonMsg> pMsg = boost::make_shared<JsonMsg>( m_buffer.begin(), m_buffer.end() ) ;

                /* reset for next message */
                m_buffer.clear() ;
                m_nMessageLength = 0 ;

                DR_LOG(log_info) << "Read JSON msg from client: " << pMsg->getRawText() << endl  ;

                switch( m_state ) {
                    case initial:
                        if( this->processAuthentication( pMsg, msgResponse ) ) {
                            m_state = authenticated ;
                        }     
                        else bDisconnect = true ;    
                        break ;              
                    default:
                        this->processMessage( pMsg, msgResponse, bDisconnect ) ;
                        break ;
                }
            } catch( std::runtime_error& err ) {
                DR_LOG(log_debug) << "Error: " << err.what() << endl ;
                m_controller.leave( shared_from_this() ) ;
                return ;
            }

            if( !msgResponse.isNull() ) {
                string strJson ;
                msgResponse.stringify(strJson) ;
                DR_LOG(log_info) << "Sending " << strJson << endl ;
                boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
            }
            if( bDisconnect ) {
                m_controller.leave( shared_from_this() ) ;
                return ;
            }
        }
        else {
#pragma mark TODO: need to deal with reading more than one message
            DR_LOG(log_debug) << "Read partial message; read " << m_buffer.size() << " of " << m_nMessageLength << " bytes" << endl ;
        }
        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
                        boost::bind( &Client::read_handler, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred ) ) ;

       
    }
    void Client::write_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) {
    	DR_LOG(log_debug) << "Wrote " << bytes_transferred << " bytes: " << endl ;
    }

    bool Client::processAuthentication( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse  ) {
        bool bReturn = false ;
        string secret ;
        string id = pMsg->get_or_default<string>("rid","") ;
        ostringstream o ;
        o << "{\"type\": \"response\", \"rid\": \"" << id << "\",\"data\": {\"authenticated\":" ;        

        if( 0 != pMsg->get_or_default<string>("type","").compare("request") || 
            0 != pMsg->get_or_default<string>("command","").compare("auth") || 
            !pMsg->get<string>("data.secret", secret)) {

            o << "false, \"reason\":\"invalid auth message\"}}" ;
        }
        else if( (bReturn = theOneAndOnlyController->isSecret( secret ) )  ) {
            o << "true}}" ;
        }
        else {
            o << "false, \"reason\":\"invalid credentials\"}}" ;
        }
        msgResponse.set( o.str() ) ;
        return bReturn ;
    }
    bool Client::processRegistration( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse  ) {
        bool bReturn = false ;
        string id = pMsg->get_or_default<string>("request-id","") ;
        ostringstream o ;
        o << "{\"type\": \"response\", \"rid\": \"" << id << "\",\"data\": {\"registered\":" ;        
        if( 0 != pMsg->get_or_default<string>("type","").compare("request") || 
            0 != pMsg->get_or_default<string>("command","").compare("register") ) {

            o << "false, \"reason\":\"invalid register message\"}}" ;
       }
       else {
            bReturn = true ;
            pMsg->get_array<string>("data.<array>.services", m_vecServices) ;
            o << "true}}" ;        
        }
        msgResponse.set( o.str() ) ;
        return bReturn ;
    }
 
    void Client::processMessage( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
        bDisconnect = false ;

        string type = pMsg->get_or_default<string>("type","") ;
        if( 0 == type.compare("notify") ) this->processNotify( pMsg, msgResponse, bDisconnect ) ;
        else if( 0 == type.compare("request") ) this->processRequest( pMsg, msgResponse, bDisconnect ) ;


    }
    void Client::processNotify( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
       bDisconnect = false ;

        string command = pMsg->get_or_default<string>("command","") ;
        string id = pMsg->get_or_default<string>("rid","") ;
        ostringstream o ;

        if( 0 == command.compare("respondToSipRequest") ) {
            int code ;
            string transactionId = pMsg->get_or_default<string>("data.transactionId","") ;
            m_controller.respondToSipRequest( transactionId, pMsg ) ;
        }
        else {
            DR_LOG(log_error) << "Unknown notify: " << command << endl ;
            bDisconnect = true ;
        }
    }
    void Client::processRequest( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
       bDisconnect = false ;

        string command = pMsg->get_or_default<string>("command","") ;
        string id = pMsg->get_or_default<string>("rid","") ;
        ostringstream o ;

        if( 0 == command.compare("route") ) {
            string verb ;
            if( !pMsg->get<string>("data.verb", verb) ) {
               DR_LOG(log_error) << "Route request must include verb " << endl ;   
               bDisconnect = true ;
               return ;         
            }
 
            if( !m_controller.wants_requests( shared_from_this(), verb ) ) {
               DR_LOG(log_error) << "Route request includes unsupported verb: " << verb << endl ;   
               bDisconnect = true ;       
               return ;        
            }
            o << "{\"type\": \"response\", \"rid\": \"" << id << "\",\"data\": {\"success\":true}}" ;     
            msgResponse.set( o.str() ) ;
    
        }
        else if( 0 == command.compare("sendSipRequest") ) {
            m_controller.sendSipRequest( shared_from_this(), pMsg, id ) ;
        }   
        else {
            DR_LOG(log_error) << "Unknown request: " << command << endl ;
            bDisconnect = true ;
        }
    }
    void Client::processResponse( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
        throw std::runtime_error("Client::processResponse not implemented") ;
    }
    void Client::sendRequestOutsideDialog( const string& transactionId, const string& msg ) {

        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendRequestWithinDialog( const string& dialogId, const string& msg ) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"dialogId\": \"" << dialogId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendResponseWithinTransaction( const string& transactionId, const string& msg ) {
       ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendDialogInfo( const string& dialogId, const string& transactionId) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "dialogCreated") ;
        o << ", \"data\": {\"dialogId\": \"" << dialogId << "\",\"transactionId\": \"" << transactionId << "\"}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendResponse( const string& rid, const string& strData) {
        ostringstream o ;
        this->pushMsgData( o, "response", NULL, rid.c_str() ) ;
        o << ", \"data\": " << strData.c_str() << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;        
    }
    void Client::sendCancelTransaction( const string& transactionId, const string& msg ) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::pushMsgData( ostringstream& o, const char* szType, const char* szCommand, const char* szRequestId) {
        string strUuid ;
        if( !szRequestId ) {
            generateUuid( strUuid ) ;
        }
        else {
            strUuid = szRequestId; 
        }
         o << "{\"type\": \"" << szType << "\", \"rid\": \"" << strUuid << "\"" ;
         if( szCommand ) o << ",\"command\": " << "\"" << szCommand << "\"";
    }

}