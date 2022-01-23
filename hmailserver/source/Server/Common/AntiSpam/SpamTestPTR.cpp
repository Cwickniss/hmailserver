// Copyright (c) 2010 Martin Knafve / hMailServer.com.  
// http://www.hmailserver.com

#include "StdAfx.h"

#include "SpamTestPTR.h"

#include "SpamTestData.h"
#include "SpamTestResult.h"

#include "AntiSpamConfiguration.h"

#include "../TCPIP/DNSResolver.h"
#include "../TCPIP/IPAddress.h"
#include "../TCPIP/LocalIPAddresses.h"

#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace HM
{
   String 
   SpamTestPTR::GetName() const
   {
      return "SpamTestPTR";
   }

   bool 
   SpamTestPTR::GetIsEnabled()
   {
      if (Configuration::Instance()->GetAntiSpamConfiguration().GetCheckPTR())
         return true;
      else
         return false;
   }

   std::set<std::shared_ptr<SpamTestResult> >
      SpamTestPTR::RunTest(std::shared_ptr<SpamTestData> pTestData)
   {
      std::set<std::shared_ptr<SpamTestResult> > setSpamTestResults;

      const IPAddress& iIPAddress = pTestData->GetOriginatingIP();

      if (LocalIPAddresses::Instance()->IsLocalIPAddress(iIPAddress))
      {
         // Ignore this test if send thru localhost.
         return setSpamTestResults;
      }

      if (!CheckPTR_(iIPAddress))
      {
         // Incorrect rDNS/PTR record
         String sMessage = "No valid rDNS/PTR record found.";
         int iScore = Configuration::Instance()->GetAntiSpamConfiguration().GetCheckPTRScore();

         std::shared_ptr<SpamTestResult> pResult = std::shared_ptr<SpamTestResult>(new SpamTestResult(GetName(), SpamTestResult::Fail, iScore, sMessage));
         setSpamTestResults.insert(pResult);   

      }
      
      return setSpamTestResults;
   }

   bool 
   SpamTestPTR::CheckPTR_(const IPAddress &ipaddress)
   {
      String _sIPAddress = ipaddress.ToString();

      std::vector<String> ptrresult;
      // do a PTR lookup
      DNSResolver dns_resolver;
      if (!dns_resolver.GetPTRRecords(_sIPAddress, ptrresult))
      {
         // DNS failure. Assume it's not spam.
         return true;
      }

      if (ptrresult.size() > 0)
      {
         // Check that the host name given in PTR actually matches
         // the senders IP address.
         std::vector<String> ipresult;
         if (dns_resolver.GetIpAddresses(ptrresult[0], ipresult, false) && ipresult.size() > 0)
         {
            // IPv4 A
            if (ipresult[0] == _sIPAddress)
            {
               return true;
            }
            // IPv6 AAAA
            else if (Configuration::Instance()->IsIPv6Available() && ipresult.size() > 1 && ipresult[1] == _sIPAddress)
            {
               return true;
            }
         }
      }

      // No (matching) PTR record. Assume it's spam.
      return false;

   }
}