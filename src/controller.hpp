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
#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/nta_stateless.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>

#include <sys/stat.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include <boost/log/common.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/syslog_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/bimap.hpp>

#include "drachtio.h"
#include "drachtio-config.hpp"
#include "client-controller.hpp"
#include "sip-dialog-controller.hpp"
#include "sip-dialog.hpp"
#include "pending-request-controller.hpp"
#include "sip-proxy-controller.hpp"
#include "ua-invalid.hpp"
#include "sip-transports.hpp"
#include "request-router.hpp"

typedef boost::mt19937 RNGType;

using namespace std ;

namespace drachtio {
	
	class DrachtioController ;

	using boost::shared_ptr;
	using boost::scoped_ptr;

  class StackMsg {
  public:
    StackMsg( const char *szLine ) ;
    ~StackMsg() {}

    void appendLine(char *szLine, bool done) ;
    bool isIncoming(void) const { return m_bIncoming; }
    bool isComplete(void) const { return m_bComplete ;}
    const string& getSipMessage(void) const { return m_sipMessage; }
    const SipMsgData_t& getSipMetaData(void) const { return m_meta; }
    const string& getFirstLine(void) const { return m_firstLine;}

  private:
    StackMsg() {}

    SipMsgData_t    m_meta ;
    bool            m_bIncoming ;
    bool            m_bComplete ;
    string          m_sipMessage ;
    string          m_firstLine ;
    ostringstream   m_os ;
  } ;

	class DrachtioController {
	public:

  	DrachtioController( int argc, char* argv[]  ) ;
  	~DrachtioController() ;

    void handleSigHup( int signal ) ;
    void handleSigTerm( int signal ) ;
  	void run() ;
  	src::severity_logger_mt<severity_levels>& getLogger() const { return *m_logger; }
    src::severity_logger_mt< severity_levels >* createLogger() ;
  
    boost::shared_ptr<DrachtioConfig> getConfig(void) { return m_Config; }
    boost::shared_ptr<SipDialogController> getDialogController(void) { return m_pDialogController ; }
    boost::shared_ptr<ClientController> getClientController(void) { return m_pClientController ; }
    boost::shared_ptr<RequestHandler> getRequestHandler(void) { return m_pRequestHandler ; }
    boost::shared_ptr<PendingRequestController> getPendingRequestController(void) { return m_pPendingRequestController ; }
    boost::shared_ptr<SipProxyController> getProxyController(void) { return m_pProxyController ; }
    su_root_t* getRoot(void) { return m_root; }
    
    enum severity_levels getCurrentLoglevel() { return m_current_severity_threshold; }

    /* network --> client messages */
    int processRequestOutsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) ;
    int processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) ;

    /* stateless callback for messages not associated with a leg */
    int processMessageStatelessly( msg_t* msg, sip_t* sip ) ;

    bool setupLegForIncomingRequest( const string& transactionId ) ;

    /* callback from http outbound requests for route selection */
    void httpCallRoutingComplete(const string& transactionId, long response_code, const string& response) ;

    bool isSecret( const string& secret ) {
    	return m_Config->isSecret( secret ) ;
    }

    nta_agent_t* getAgent(void) { return m_nta; }
    su_home_t* getHome(void) { return m_home; }

    void getMyHostports( vector<string>& vec ) ;

    bool getMySipAddress( const char* proto, string& host, string& port, bool ipv6 = false ) ;

    void printStats(void) ;
    void processWatchdogTimer(void) ;

    const tport_t* getTportForProtocol( const string& remoteHost, const char* proto ) ;

    sip_time_t getTransactionTime( nta_incoming_t* irq ) ;
    void getTransactionSender( nta_incoming_t* irq, string& host, unsigned int& port ) ;

    void setLastSentStackMessage(shared_ptr<StackMsg> msg) { m_lastSentMsg = msg; }
    void setLastRecvStackMessage(shared_ptr<StackMsg> msg) { m_lastRecvMsg = msg; }

    bool isDaemonized(void) { return m_bDaemonize; }
    void cacheTportForSubscription( const char* user, const char* host, int expires, tport_t* tp ) ; 
    void flushTportForSubscription( const char* user, const char* host ) ; 
    boost::shared_ptr<UaInvalidData> findTportForSubscription( const char* user, const char* host ) ;

    RequestRouter& getRequestRouter(void) { return m_requestRouter; }

    void makeOutboundConnection(const string& transactionId, const string& uri);

	private:

  	DrachtioController() ;

  	bool parseCmdArgs( int argc, char* argv[] ) ;
  	void usage() ;
  	
  	void daemonize() ;
  	void initializeLogging() ;
  	void deinitializeLogging() ;
  	bool installConfig() ;
  	void logConfig() ;
    int validateSipMessage( sip_t const *sip ) ;

    void processRejectInstruction(const string& transactionId, unsigned int status, const char* reason = NULL) ;
    void processRedirectInstruction(const string& transactionId, vector<string>& vecContact) ;
    void processProxyInstruction(const string& transactionId, bool recordRoute, bool followRedirects, 
        bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, vector<string>& vecDestination) ;
    void processOutboundConnectionInstruction(const string& transactionId, const char* uri) ;

    void finishRequest( const string& transactionId, const boost::system::error_code& err, 
        unsigned int status_code, const string& body) ;


  	scoped_ptr< src::severity_logger_mt<severity_levels> > m_logger ;
  	boost::mutex m_mutexGlobal ;
  	boost::shared_mutex m_mutexConfig ; 
  	bool m_bLoggingInitialized ;
  	string m_configFilename ;
          
    string  m_user ;    //system user to run as
    unsigned int m_adminPort; //if provided on command-line overrides config file setting
    string m_sipContact; //if provided on command line overrides config file setting

    string m_publicAddress ;

    shared_ptr< sinks::synchronous_sink< sinks::syslog_backend > > m_sinkSysLog ;
    shared_ptr<  sinks::synchronous_sink< sinks::text_file_backend > > m_sinkTextFile ;
    shared_ptr<  sinks::synchronous_sink< sinks::text_ostream_backend > > m_sinkConsole ;

    shared_ptr<DrachtioConfig> m_Config, m_ConfigNew ;
    int m_bDaemonize ;
    int m_bNoConfig ;
    int m_bConsoleLogging;

    severity_levels m_current_severity_threshold ;
    int m_nSofiaLoglevel ;
    string m_strHomerAddress;
    unsigned int m_nHomerPort;
    uint32_t m_nHomerId;

    shared_ptr<ClientController> m_pClientController ;
    shared_ptr<RequestHandler> m_pRequestHandler ;
    shared_ptr<SipDialogController> m_pDialogController ;
    shared_ptr<SipProxyController> m_pProxyController ;
    shared_ptr<PendingRequestController> m_pPendingRequestController ;

    shared_ptr<StackMsg> m_lastSentMsg ;
    shared_ptr<StackMsg> m_lastRecvMsg ;


    su_home_t* 	m_home ;
    su_root_t* 	m_root ;
    su_timer_t*     m_timer ;
    nta_agent_t*	m_nta ;
    nta_leg_t*      m_defaultLeg ;
  	su_clone_r 	m_clone ;

    vector< boost::shared_ptr<SipTransport> >  m_vecTransports;
    
    typedef boost::unordered_map<string, boost::shared_ptr<UaInvalidData> > mapUri2InvalidData ;
    mapUri2InvalidData m_mapUri2InvalidData ;

    bool    m_bIsOutbound ;
    string  m_strRequestServer ;
    string  m_strRequestPath ;

    RequestRouter   m_requestRouter ;
  } ;

} ;


#endif //__CONTROLLER_H__
