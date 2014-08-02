// Copyright (c) 2010 Martin Knafve / hMailServer.com.  
// http://www.hmailserver.com

#include "StdAfx.h"

#include "IOService.h"

#include "TCPServer.h"
#include "TCPConnection.h"

#include "LocalIPAddresses.h"

#include "../Threading/WorkQueue.h"
#include "../Threading/WorkQueueManager.h"
#include "../Util/ByteBuffer.h"
#include "../Util/ServerStatus.h"
#include "../BO/TCPIPPort.h"
#include "../BO/TCPIPPorts.h"

#include "IOQueueWorkerTask.h"
#include "SocketConstants.h"
#include "SslContextInitializer.h"

#include "../../SMTP/SMTPConnection.h"
#include "../../IMAP/IMAPConnection.h"
#include "../../POP3/POP3Connection.h"

#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace HM
{
   IOService::IOService(void) :
       Task("IOService"),
       client_context_(io_service_, boost::asio::ssl::context::sslv23)
   {
      
   }

   IOService::~IOService(void)
   {
      LOG_DEBUG("IOService::~IOService - Destructing");
   }

   bool 
   IOService::RegisterSessionType(SessionType st)
   //---------------------------------------------------------------------------()
   // DESCRIPTION:
   // Registers a new connection type.
   //---------------------------------------------------------------------------()
   {
      m_setSessionTypes.insert(st);

      return true;
   }

   void 
   IOService::Initialize()
   {
      SslContextInitializer::InitClient(client_context_);
   }

   void
   IOService::DoWork()
   //---------------------------------------------------------------------------()
   // DESCRIPTION:
   // Creates the IO completion port, creates the worker threads, listen sockets etc.
   //---------------------------------------------------------------------------()
   {
      SetIsStarted();

      

      LOG_DEBUG("IOService::Start()");

      // Make sure information on which local ports are in use is reset.
      LocalIPAddresses::Instance()->LoadIPAddresses();

      // Create one socket for each IP address specified in the multi-homing settings.
      vector<shared_ptr<TCPIPPort> > vecTCPIPPorts = Configuration::Instance()->GetTCPIPPorts()->GetVector();

      vector<shared_ptr<TCPIPPort> >::iterator iterPort = vecTCPIPPorts.begin();
      vector<shared_ptr<TCPIPPort> >::iterator iterPortEnd = vecTCPIPPorts.end();


      for (; iterPort != iterPortEnd; iterPort++)
      {
         shared_ptr<TCPIPPort> pPort = (*iterPort);
         IPAddress address = pPort->GetAddress();
         int iPort = pPort->GetPortNumber();
         SessionType st = pPort->GetProtocol();
         ConnectionSecurity connection_security = pPort->GetConnectionSecurity();

         shared_ptr<SSLCertificate> pSSLCertificate;

         if (connection_security == CSSSL ||
             connection_security == CSSTARTTLSOptional ||
             connection_security == CSSTARTTLSRequired)
         {
            shared_ptr<SSLCertificates> pSSLCertificates = Configuration::Instance()->GetSSLCertificates();
            pSSLCertificate = pSSLCertificates->GetItemByDBID(pPort->GetSSLCertificateID());
         }

         if (m_setSessionTypes.find(st) == m_setSessionTypes.end())
            continue;

         shared_ptr<TCPConnectionFactory> pConnectionFactory;
         shared_ptr<TCPServer> pTCPServer;

         switch (st)
         {
         case STSMTP:
            pConnectionFactory = shared_ptr<TCPConnectionFactory>(new SMTPConnectionFactory());
            break;
         case STIMAP:
            pConnectionFactory = shared_ptr<TCPConnectionFactory>(new IMAPConnectionFactory());
            break;
         case STPOP3:
            pConnectionFactory = shared_ptr<TCPConnectionFactory>(new POP3ConnectionFactory());
            break;

         default:
            ErrorManager::Instance()->ReportError(ErrorManager::Medium, 4325, "IOService::DoWork()", "Unable to start server- Unsupported session type.");
            break;
         }

         pTCPServer = shared_ptr<TCPServer>(new TCPServer(io_service_, address, iPort, st, pSSLCertificate, pConnectionFactory, connection_security));

         pTCPServer->Run();

         tcp_servers_.push_back(pTCPServer);
      }


      const int iThreadCount = Configuration::Instance()->GetTCPIPThreads();

      if (iThreadCount <= 0)
         ErrorManager::Instance()->ReportError(ErrorManager::Medium, 4325, "IOService::DoWork()", "The number of TCP/IP threads has been set to zero.");

      int iQueueID = WorkQueueManager::Instance()->CreateWorkQueue(iThreadCount, "IOCPQueue");

      shared_ptr<WorkQueue> pWorkQueue = WorkQueueManager::Instance()->GetQueue("IOCPQueue");

      // Launch a thread that holds the IOCP objects
      for (int i = 0; i < iThreadCount; i++)
      {
         shared_ptr<IOCPQueueWorkerTask> pWorkerTask = shared_ptr<IOCPQueueWorkerTask>(new IOCPQueueWorkerTask(io_service_));
         WorkQueueManager::Instance()->AddTask(iQueueID, pWorkerTask);
      }	

      try
      {
         boost::mutex do_work_dummy_mutex;
         boost::mutex::scoped_lock lock(do_work_dummy_mutex);
         do_work_dummy.wait(lock);

      }
      catch (thread_interrupted const&)
      {
         boost::this_thread::disable_interruption disabled;

         LOG_DEBUG("IOService::Stop()");
         io_service_.stop();

         vector<shared_ptr<TCPServer> >::iterator iterServer = tcp_servers_.begin();
         vector<shared_ptr<TCPServer> >::iterator iterEnd = tcp_servers_.end();
         for (; iterServer != iterEnd; iterServer++)
         {
            (*iterServer)->StopAccept();
         }

         LOG_DEBUG("IOService::DoWork() - removing Queue IOCP Queue")
            // Now the worker queues will get notifications that the outstanding
            // acceptex sockets are dropped.
         WorkQueueManager::Instance()->RemoveQueue("IOCPQueue");

         LOG_DEBUG("IOService::Stop() - Complete");
         return;
      }

   }

   boost::asio::ssl::context &
   IOService::GetClientContext()
   {
      return client_context_;
   }

   boost::asio::io_service &
   IOService::GetIOService()
   {
      return io_service_;
   }


}