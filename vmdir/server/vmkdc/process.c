/*
 * Copyright © 2012-2015 VMware, Inc.  All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the “License”); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an “AS IS” BASIS, without
 * warranties or conditions of any kind, EITHER EXPRESS OR IMPLIED.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#include "includes.h"

static
DWORD
VmKdcProcessAsReq(
    PVMKDC_CONTEXT pContext,
    PVMKDC_DATA *ppKrbMsg);

static
DWORD
VmKdcProcessTgsReq(
    PVMKDC_CONTEXT pContext,
    PVMKDC_DATA *ppkrbMsg);

static
BOOLEAN
isCrossRealmTGT(
    PVMKDC_PRINCIPAL pPrincipal);

static
DWORD
VmDirGetClosestServer(
    PVMKDC_CONTEXT pContext,
    PVMKDC_PRINCIPAL pServer,
    PVMKDC_PRINCIPAL *ppClosestServer);

DWORD
VmKdcProcessKdcReq(
    PVMKDC_CONTEXT pContext,
    PVMKDC_DATA *ppKrbMsg)
{
    DWORD dwError = 0;
    if (pContext->pRequest->requestBuf[0] == VMKDC_MSG_TAG_AS_REQ)
    {
        dwError = VmKdcProcessAsReq(pContext, ppKrbMsg);
        BAIL_ON_VMKDC_ERROR(dwError);
    }
    else if (pContext->pRequest->requestBuf[0] == VMKDC_MSG_TAG_TGS_REQ)
    {
        dwError = VmKdcProcessTgsReq(pContext, ppKrbMsg);
        BAIL_ON_VMKDC_ERROR(dwError);
    }
    else
    {
        dwError = ERROR_INVALID_PARAMETER;
        BAIL_ON_VMKDC_ERROR(dwError);
    }

error:
    return dwError;
}

#if defined(VMDIR_ENABLE_PAC) || defined(WINJOIN_CHECK_ENABLED)
static
DWORD
VmKdcCreateAuthzData(
    PVMKDC_CONTEXT pContext,
    PVMKDC_PRINCIPAL pPrincipal,
    DWORD tTime,
    PVMKDC_KEY pServerKey,
    PVMKDC_KEY pPrivateKey,
    PVMKDC_AUTHZDATA *ppAuthzData)
{
    DWORD dwError = 0;
    PVMKDC_AUTHZDATA pAuthzDataPac = NULL;
    PVMKDC_AUTHZDATA pAuthzDataIfRelevant = NULL;
    PVMKDC_AUTHZ_PAC pPAC = NULL;
    PVMKDC_DATA pPACInfoData = NULL;
    PVMKDC_DATA pPACData = NULL;
    PVMKDC_DATA pDataVmdirPac = NULL;
    PVMKDC_AUTHZ_PAC_INFO pPACInfo = NULL;
    long access_info_size = 0;
    unsigned char *access_info_data = NULL;
    PSTR pszName = NULL;
#ifdef WINJOIN_CHECK_ENABLED
    KERB_VALIDATION_INFO  *pInfo = NULL;
#else
    PVMDIR_AUTHZ_INFO pInfo = NULL;
#endif

    dwError = VmKdcAllocatePAC(
                      pContext,
                      &pPAC);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcUnparsePrincipalName(
                      pPrincipal,
                      &pszName);
    BAIL_ON_VMKDC_ERROR(dwError);

#ifdef WINJOIN_CHECK_ENABLED
    dwError = VmDirKrbGetKerbValidationInfo(
                  pszName,
                  &pInfo);
#else
    dwError = VmDirKrbGetAuthzInfo(
                      pszName,
                      &pInfo);
    BAIL_ON_VMKDC_ERROR(dwError);
#endif

    /* NDR encode the access info data */

    dwError = VmKdcEncodeAuthzInfo(
                      pInfo,
                      &access_info_size,
                      (PVOID)&access_info_data);
    BAIL_ON_VMKDC_ERROR(dwError);

    /* Add the encoded access info data to a PAC record */

    dwError = VmKdcAllocateData(
                      access_info_data,
                      access_info_size,
                      &pPACInfoData);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcAllocatePACInfo(
                      VMKDC_PAC_ACCESS_INFO,
                      pPACInfoData,
                      &pPACInfo);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcAddPACInfoPAC(
                      pPAC,
                      pPACInfo);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcSignPAC(
                      pPAC,
                      tTime,
                      pPrincipal,
                      pServerKey,
                      pPrivateKey,
                      &pPACData);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcAllocateAuthzData(&pAuthzDataPac);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcAddAuthzData(
                      pAuthzDataPac,
                      pPACData,
#ifdef WINJOIN_CHECK_ENABLED
                      VMKDC_AUTHZDATA_WIN2K_PAC /* ms-pac */
#else
                      VMKDC_AUTHZDATA_VMDIR_PAC /* vmdir-pac */
#endif
                      );
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcEncodeAuthzData(
                      pAuthzDataPac,
                      &pDataVmdirPac
                      );
    BAIL_ON_VMKDC_ERROR(dwError);

    /* ASN.1 encode the PAC in another layer of authorization data
     * with type AD-IF-RELEVANT
     */

    dwError = VmKdcAllocateAuthzData(&pAuthzDataIfRelevant);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcAddAuthzData(
                      pAuthzDataIfRelevant,
                      pDataVmdirPac,
                      VMKDC_AUTHZDATA_AD_IF_RELEVANT);
    BAIL_ON_VMKDC_ERROR(dwError);

    *ppAuthzData = pAuthzDataIfRelevant;

cleanup:

    if (pPAC)
    {
        VmKdcDestroyPAC(pPAC);
    }
    if (access_info_data)
    {
        free(access_info_data);
    }
    if (pPACInfo)
    {
        VmKdcDestroyPACInfo(pPACInfo);
    }
    VMKDC_SAFE_FREE_STRINGA(pszName);
    VMKDC_SAFE_FREE_DATA(pPACData);
    VMKDC_SAFE_FREE_DATA(pPACInfoData);
    VMKDC_SAFE_FREE_DATA(pDataVmdirPac);
    VMKDC_SAFE_FREE_AUTHZDATA(pAuthzDataPac);
    if (pInfo)
    {
#ifdef WINJOIN_CHECK_ENABLED
        VmDirKrbFreeKerbValidationInfo(pInfo);
#else
        VmDirKrbFreeAuthzInfo(pInfo);
#endif
    }

    return dwError;

error:

    goto cleanup;
}

#endif // VMDIR_ENABLE_PAC

static
DWORD
VmKdcMakeSaltForEtype(
    PSTR          pszPrincName,
    VMKDC_ENCTYPE etype,
    PSTR          *ppszSaltString)
{
    DWORD dwError = 0;
    PSTR pszDupPrincName = NULL;
    PSTR pszRetSaltString = NULL;
    PSTR pszSaltDomain = NULL;
    PSTR pszSaltUser = NULL;
    PSTR pszTmp = NULL;

    /* Construct correct salt type for etype */

    switch (etype)
    {
        case ENCTYPE_AES256_CTS_HMAC_SHA1_96:
            dwError = VmDirAllocateStringPrintf(
                          &pszDupPrincName,
                          "%s",
                          pszPrincName);
            BAIL_ON_VMKDC_ERROR(dwError);

            pszSaltUser = pszDupPrincName;

            pszTmp = VmKdcStringChrA(pszDupPrincName, (int) '@');
            if (!pszTmp)
            {
                /* Nothing to do */
                goto cleanup;
            }
            *pszTmp++ = '\0';
            pszSaltDomain = pszTmp;

            dwError = VmDirAllocateStringPrintf(
                          &pszRetSaltString,
                          "%s%s",
                          pszSaltDomain,
                          pszSaltUser);
            BAIL_ON_VMKDC_ERROR(dwError);
        break;
        default:
        break;
    }
    *ppszSaltString = pszRetSaltString;

cleanup:

    VMKDC_SAFE_FREE_STRINGA(pszDupPrincName);
    return dwError;

error:
    VMKDC_SAFE_FREE_STRINGA(pszRetSaltString);
    goto cleanup;
}

static
DWORD
VmKdcProcessAsReq(
    PVMKDC_CONTEXT pContext,
    PVMKDC_DATA *ppKrbMsg)
{
    DWORD dwError = 0;
    DWORD dwError2 = 0;
    PVMKDC_ASREQ asRequest = NULL;
    PVMKDC_DATA krbMsg = NULL;
    PVMKDC_KEY pCKey = NULL;
    PVMKDC_KEY pSKey = NULL;
    PVMKDC_KEY pSessionKey = NULL;
    PVMKDC_PRINCIPAL pCname = NULL;
    PVMKDC_PRINCIPAL pSname = NULL;
    DWORD nonce = 0;
    PVMKDC_DATA pAsnData = NULL;
    PVMKDC_TICKET pTicket = NULL;
    PVMKDC_ASREP pAsRep = NULL;
    time_t t_start = 0;
    time_t t_end = 0;
    time_t *t_reqTill = NULL;
    time_t *renew_till = NULL;
    PVMKDC_DIRECTORY_ENTRY pClientEntry = NULL;
    PVMKDC_DIRECTORY_ENTRY pServerEntry = NULL;
    DWORD error_code = 0;
    PVMKDC_DATA e_data = NULL;
    PSTR pszClientName = NULL;
    VMKDC_TICKET_FLAGS flags = 0;
    time_t kdc_time = 0;
    time_t maxrt = 0;
    BOOLEAN renewable_ok = 0;
    VMKDC_ENCTYPE etype = 0;
    unsigned int i = 0;
#if defined(VMDIR_ENABLE_PAC) || defined(WINJOIN_CHECK_ENABLED)
    PVMKDC_AUTHZDATA pAuthzData = NULL;
#else
    PVOID pAuthzData = NULL;
#endif
    DWORD dwSaltStringLen = 0;
    PSTR pszSaltString = NULL;
    PSTR pszSaltStringClientName = NULL;
    PVMKDC_SALT pSalt = NULL;
    DWORD bSaltMatch = FALSE;

    dwError = VmKdcAllocateData(
                  pContext->pRequest->requestBuf,
                  pContext->pRequest->requestBufLen,
                  &pAsnData);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcDecodeAsReq(pAsnData, &asRequest);
    BAIL_ON_VMKDC_ERROR(dwError);

    pCname = asRequest->req_body.cname;
    pSname = asRequest->req_body.sname;
    nonce = asRequest->req_body.nonce;

    etype = asRequest->req_body.etype.type[0];
/* RFC4120 Page 26
 * If the requested starttime is absent, indicates a time in the past,
 * or is within the window of acceptable clock skew for the KDC and the
 * POSTDATE option has not been specified, then the starttime of the
 * ticket is set to the authentication server's current time.  If it
 * indicates a time in the future beyond the acceptable clock skew, but
 * the POSTDATED option has not been specified, then the error
 * KDC_ERR_CANNOT_POSTDATE is returned.  Otherwise the requested
 * starttime is checked against the policy of the local realm (the
 * administrator might decide to prohibit certain types or ranges of
 * postdated tickets), and if the ticket's starttime is acceptable, it
 * is set as requested, and the INVALID flag is set in the new ticket.
 * The postdated ticket MUST be validated before use by presenting it to
 * the KDC after the starttime has been reached.
 */

    kdc_time = time(NULL);
    if (!asRequest->req_body.from ||
        *asRequest->req_body.from < kdc_time ||
        (abs((long) (*asRequest->req_body.from - kdc_time)) <
         pContext->pGlobals->iClockSkew))

    {
        t_start = kdc_time;
    }
    else
    {
        dwError = ERROR_CANNOT_POSTDATE;
        BAIL_ON_VMKDC_ERROR(dwError);
    }

/*
 * The expiration time of the ticket will be set to the earlier of the
 * requested endtime and a time determined by local policy, possibly by
 * using realm- or principal-specific factors.  For example, the
 * expiration time MAY be set to the earliest of the following:
 *
 *    *  The expiration time (endtime) requested in the KRB_AS_REQ
 *       message.
 *
 *    *  The ticket's starttime plus the maximum allowable lifetime
 *       associated with the client principal from the authentication
 *       server's database.
 *
 *    *  The ticket's starttime plus the maximum allowable lifetime
 *       associated with the server principal.
 *
 *    *  The ticket's starttime plus the maximum lifetime set by the
 *       policy of the local realm.
 *
 * If the requested expiration time minus the starttime (as determined
 * above) is less than a site-determined minimum lifetime, an error
 * message with code KDC_ERR_NEVER_VALID is returned.  If the requested
 * expiration time for the ticket exceeds what was determined as above,
 * and if the 'RENEWABLE-OK' option was requested, then the 'RENEWABLE'
 * flag is set in the new ticket, and the renew-till value is set as if
 * the 'RENEWABLE' option were requested (the field and option names are
 * described fully in Section 5.4.1).
 */

    t_end = t_start + pContext->pGlobals->iMaxLife;
    t_reqTill = asRequest->req_body.till;
    if (t_reqTill && *t_reqTill != 0)
    {
        if (*t_reqTill > t_end)
        {
            renewable_ok = VMKDC_FLAG_ISSET(asRequest->req_body.kdc_options,
                                            VMKDC_KO_RENEWABLE_OK);
            if (renewable_ok)
            {
                maxrt = t_start + pContext->pGlobals->iMaxRenewableLife;
                if (*t_reqTill <= maxrt)
                {
                    renew_till = t_reqTill;
                }
                else
                {
                    renew_till = &maxrt;
                }
                VMKDC_FLAG_SET(flags, VMKDC_TF_RENEWABLE);
            }
        }
        else
        {
            t_end = *t_reqTill;
        }
    }
    if ((t_end - t_start) < pContext->pGlobals->iClockSkew)
    {
        dwError = ERROR_NEVER_VALID;
        BAIL_ON_VMKDC_ERROR(dwError);
    }

    dwError = VmKdcUnparsePrincipalName(pCname, &pszClientName);
    BAIL_ON_VMKDC_ERROR(dwError);

    VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL,
             "Received AS-REQ for client %s",
             pszClientName);

    /*
     * Get the client key and case-sensitive UPN name
     */
    dwError = VmKdcSearchDirectory(
                  pContext,
                  pCname,
                  &pClientEntry);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Select one of two supported ENCTYPEs (prefer AES256)
     */
    for (i=0; i<asRequest->req_body.etype.count; i++)
    {
        if (asRequest->req_body.etype.type[i] == ENCTYPE_AES256_CTS_HMAC_SHA1_96)
        {
            etype = ENCTYPE_AES256_CTS_HMAC_SHA1_96;
            dwError = VmKdcMakeSaltForEtype(
                          pClientEntry->princName,
                          etype,
                          &pszSaltString);
            BAIL_ON_VMKDC_ERROR(dwError);
            dwSaltStringLen = VmKdcStringLenA(pszSaltString);

            dwError = VmKdcMakeSaltForEtype(
                          pszClientName,
                          etype,
                          &pszSaltStringClientName);
            BAIL_ON_VMKDC_ERROR(dwError);

            /* Test Canonicalized salt and presented UPN for case match */
            if (pszSaltString && pszSaltStringClientName)
            {
                bSaltMatch = VmKdcStringCompareA(
                                 pszSaltString,
                                 pszSaltStringClientName,
                                 TRUE);
                BAIL_ON_VMKDC_ERROR(dwError);
            }

            break;
        }
        if (asRequest->req_body.etype.type[i] == ENCTYPE_ARCFOUR_HMAC_MD5)
        {
            etype = ENCTYPE_ARCFOUR_HMAC_MD5;
        }
    }

    dwError = VmKdcFindKeyByEType(
                  pClientEntry,
                  etype,
                  &pCKey);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Get the server key
     */
    dwError = VmKdcSearchDirectory(
                  pContext,
                  pSname,
                  &pServerEntry);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcFindKeyByEType(
                  pServerEntry,
                  etype,
                  &pSKey);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Verify the preauthentication data.
     */
    dwError = VmKdcVerifyAsReqPaData(pContext, asRequest, pCKey);
    if (dwError == ERROR_NO_PREAUTH ||
        (dwError == ERROR_FAILED_PREAUTH && !bSaltMatch))
    {
        dwError2 = VmKdcMakeSalt(
                       etype,
                       dwSaltStringLen,
                       pszSaltString,
                       &pSalt);
        BAIL_ON_VMKDC_ERROR(dwError2);

        dwError2 = VmKdcBuildKrbErrorEData(pCKey, pSalt, NULL, &e_data);
        BAIL_ON_VMKDC_ERROR(dwError2);
    }
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Create a random session key
     */
    dwError = VmKdcRandomKey(pContext,
                             etype,
                             &pSessionKey);
    BAIL_ON_VMKDC_ERROR(dwError);

    VMKDC_FLAG_SET(flags, VMKDC_TF_INITIAL);
    VMKDC_FLAG_SET(flags, VMKDC_TF_PRE_AUTHENT);
    if (VMKDC_FLAG_ISSET(asRequest->req_body.kdc_options, VMKDC_KO_FORWARDABLE))
    {
        VMKDC_FLAG_SET(flags, VMKDC_TF_FORWARDABLE);
    }
    if (VMKDC_FLAG_ISSET(asRequest->req_body.kdc_options, VMKDC_KO_PROXIABLE))
    {
        VMKDC_FLAG_SET(flags, VMKDC_TF_PROXIABLE);
    }

#if 0
    /*
     * Create authorization data.
     */
    dwError = VmKdcCreateAuthzData(
                      pContext,
                      pCname,
                      kdc_time,
                      pSKey,
                      pSKey,
                      &pAuthzData);
    BAIL_ON_VMKDC_ERROR(dwError);
#endif

    /*
     * Build a TICKET
     */
    dwError = VmKdcBuildTicket(
                      pContext,
                      pCname,
                      pSname,
                      pSKey,      /* key */
                      pSessionKey,
                      flags,      /* flags */
                      NULL,       /* transited */
                      t_start,    /* authtime */
                      &t_start,   /* starttime */
                      t_end,      /* endtime */
                      renew_till, /* renew_till */
                      NULL,       /* caddr */
                      pAuthzData, /* authorization_data */
                      &pTicket);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Build an AS-REP
     */
    dwError = VmKdcBuildAsRep(
                      pContext,
                      pCname,
                      pSname,
                      pCKey,      /* key */
                      pSessionKey,
                      pTicket,
                      NULL,       /* last-req */
                      nonce,      /* nonce */
                      NULL,       /* key-expiration (optional) */
                      flags,      /* flags */
                      t_start,    /* authtime */
                      &t_start,   /* starttime (optional) */
                      t_end,      /* endtime */
                      renew_till, /* renew-till (optional) */
                      NULL,       /* caddr */
                      &pAsRep);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * ASN.1 encode the AS-REP, and send the response.
     */
    dwError = VmKdcEncodeAsRep(pAsRep, &krbMsg);
    BAIL_ON_VMKDC_ERROR(dwError);

    VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL,
             "Sending AS-REP for client %s",
             pszClientName);

    *ppKrbMsg = krbMsg;

error:
    switch (dwError)
    {
        case 0: /* success, don't set error_code */
            break;
        case ERROR_NO_PREAUTH:
            error_code = VMKDC_KDC_ERR_PREAUTH_REQUIRED;
            break;
        case ERROR_FAILED_PREAUTH:
            error_code = VMKDC_KDC_ERR_PREAUTH_FAILED;
            break;
        case VMKDC_RPC_SERVER_NOTAVAIL:
            /*
             * Clear the global masterKey so it can't be used when vmdir is unavailable.
             */
            VmKdcTerminateDirectory(pContext->pGlobals);
            VmKdcdStateSet(VMKDCD_STARTUP);
            error_code = VMKDC_KDC_ERR_SVC_UNAVAILABLE;
            break;
        case ERROR_CANNOT_POSTDATE:
            error_code = VMKDC_KDC_ERR_CANNOT_POSTDATE;
            break;
        case ERROR_NEVER_VALID:
            error_code = VMKDC_KDC_ERR_NEVER_VALID;
            break;
        case ERROR_NO_PRINC:
        case ERROR_NO_KEY_ETYPE:
        default:
            error_code = VMKDC_KDC_ERR_C_PRINCIPAL_UNKNOWN;
            break;
    }
    if (asRequest && pCname && pSname && error_code)
    {
        dwError = VmKdcBuildKrbError(
                          asRequest->pvno,
                          NULL, /* ctime */
                          time(NULL), /* stime */
                          error_code, /* error_code */
                          VMKDC_GET_PTR_DATA(pCname->realm), /* crealm */
                          pCname, /* cname */
                          VMKDC_GET_PTR_DATA(pSname->realm), /* realm */
                          pSname, /* sname */
                          NULL, /* e_text */
                          e_data, /* e_data */
                          &krbMsg);
        if (dwError == 0)
        {
            *ppKrbMsg = krbMsg;

            VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL,
                     "Sending KRB-ERROR %d for client %s",
                     error_code, pszClientName);
        }
        else
        {
            VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL,
                     "Failed to build KRB-ERROR %d for client %s",
                     error_code, pszClientName);
        }
    }
    else if (dwError)
    {
        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL,
                 "VmKdcProcessAsRequest failed, error code: (%u)",
                 dwError);
    }
    VMKDC_SAFE_FREE_DATA(e_data);
    VMKDC_SAFE_FREE_DIRECTORY_ENTRY(pClientEntry);
    VMKDC_SAFE_FREE_DIRECTORY_ENTRY(pServerEntry);
    VMKDC_SAFE_FREE_ASREQ(asRequest);
    VMKDC_SAFE_FREE_ASREP(pAsRep);
    VMKDC_SAFE_FREE_KEY(pSessionKey);
    VMKDC_SAFE_FREE_TICKET(pTicket);
    VMKDC_SAFE_FREE_DATA(pAsnData);
    VMKDC_SAFE_FREE_STRINGA(pszClientName);
    VMKDC_SAFE_FREE_STRINGA(pszSaltString);
    VMKDC_SAFE_FREE_STRINGA(pszSaltStringClientName);
#if defined(VMDIR_ENABLE_PAC) || defined(WINJOIN_CHECK_ENABLED)
    VMKDC_SAFE_FREE_AUTHZDATA(pAuthzData);
#endif
    return dwError;
}

static
DWORD
VmKdcProcessTgsReq(
    PVMKDC_CONTEXT pContext,
    PVMKDC_DATA *ppKrbMsg)
{
    DWORD dwError = 0;
    DWORD error_code = 0;
    PVMKDC_ASREQ tgsRequest = NULL;
    PVMKDC_DATA krbMsg = NULL;
    PVMKDC_KEY pSKey = NULL;
    PVMKDC_KEY pPresentedSKey = NULL;
    PVMKDC_KEY pSessionKey = NULL;
    PVMKDC_PRINCIPAL pSname = NULL;
    PVMKDC_PRINCIPAL pClosestServer = NULL;
    DWORD nonce = 0;
    PVMKDC_DATA pAsnData = NULL;
    PVMKDC_TICKET pTicket = NULL;
    PVMKDC_TGSREP pTgsRep = NULL;
    time_t t_start = 0;
    time_t t_end = 0;
    time_t *t_reqTill = NULL;
    time_t *renew_till = NULL;
    time_t kdc_time = 0;
    PVMKDC_METHOD_DATA pMethodData = NULL;
    PVMKDC_PADATA pPaData = NULL;
    PVMKDC_APREQ apReq = NULL;
    PVMKDC_ENCTICKETPART pEncTicketPart = NULL;
    PVMKDC_DATA pData = NULL;
    PVMKDC_AUTHENTICATOR pAuthenticator = NULL;
    PVMKDC_DIRECTORY_ENTRY pServerEntry = NULL;
    PVMKDC_DIRECTORY_ENTRY pDirectoryEntry = NULL;
    PSTR pszServerName = NULL;
    VMKDC_TICKET_FLAGS flags = 0;
    BOOLEAN renew = FALSE;
    VMKDC_ENCTYPE etype = 0;
    unsigned int i = 0;
#if defined(VMDIR_ENABLE_PAC) || defined(WINJOIN_CHECK_ENABLED)
    PVMKDC_KEY pKrbtgtKey = NULL;
    PVMKDC_DIRECTORY_ENTRY pKrbtgtEntry = NULL;
    PVMKDC_AUTHZDATA pAuthzData = NULL;
#else
    PVOID pAuthzData = NULL;
#endif

    dwError = VmKdcAllocateData(
                  pContext->pRequest->requestBuf,
                  pContext->pRequest->requestBufLen,
                  &pAsnData);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcDecodeTgsReq(pAsnData, &tgsRequest);
    BAIL_ON_VMKDC_ERROR(dwError);

    pSname = tgsRequest->req_body.sname;
    nonce = tgsRequest->req_body.nonce;
    pMethodData = tgsRequest->padata;

    etype = tgsRequest->req_body.etype.type[0];

    dwError = VmKdcUnparsePrincipalName(pSname, &pszServerName);
    BAIL_ON_VMKDC_ERROR(dwError);

    VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL,
            "Received TGS-REQ for server %s",
            pszServerName);

    /*
     * Get the server key
     */
    dwError = VmKdcSearchDirectory(
                  pContext,
                  pSname,
                  &pServerEntry);
    if (dwError == ERROR_NO_PRINC && isCrossRealmTGT(pSname))
    {
        /*
         * Look for a cross-realm krbtgt that may be closer.
         */
        dwError = VmDirGetClosestServer(
                      pContext,
                      pSname,
                      &pClosestServer);
        BAIL_ON_VMKDC_ERROR(dwError);

        dwError = VmKdcSearchDirectory(
                      pContext,
                      pClosestServer,
                      &pServerEntry);
        BAIL_ON_VMKDC_ERROR(dwError);

        pSname = pClosestServer;

        VMKDC_SAFE_FREE_STRINGA(pszServerName);

        dwError = VmKdcUnparsePrincipalName(pSname, &pszServerName);
        BAIL_ON_VMKDC_ERROR(dwError);
    }
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Select one of two supported ENCTYPEs (prefer AES256)
     */
    for (i=0; i<tgsRequest->req_body.etype.count; i++)
    {
        if (tgsRequest->req_body.etype.type[i] == ENCTYPE_AES256_CTS_HMAC_SHA1_96)
        {
            etype = ENCTYPE_AES256_CTS_HMAC_SHA1_96;
            break;
        }
        if (tgsRequest->req_body.etype.type[i] == ENCTYPE_ARCFOUR_HMAC_MD5)
        {
            etype = ENCTYPE_ARCFOUR_HMAC_MD5;
        }
    }

    dwError = VmKdcFindKeyByEType(
                  pServerEntry,
                  etype,
                  &pSKey);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Get the preauth
     */
    if (!pMethodData)
    {
        dwError = ERROR_PROTOCOL;
        BAIL_ON_VMKDC_ERROR(dwError);
    }
    dwError = VmKdcFindPaData(VMKDC_PADATA_AP_REQ, pMethodData, &pPaData);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Decode the AP-REQ
     */
    dwError = VmKdcDecodeApReq(pPaData->data, &apReq);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Get the key for the server principal in the ticket (krbtgt).
     */
    dwError = VmKdcSearchDirectory(
                  pContext,
                  apReq->ticket->sname,
                  &pDirectoryEntry);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcFindKeyByEType(
                  pDirectoryEntry,
                  etype,
                  &pPresentedSKey);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Decrypt the encrypted part of the ticket
     */
    dwError = VmKdcDecryptEncData(pContext,
                                  pPresentedSKey,
                                  VMKDC_KU_TICKET,
                                  apReq->ticket->enc_part,
                                  &pData);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Decode the encrypted part of the ticket
     */
    dwError = VmKdcDecodeEncTicketPart(pData, pSKey, &pEncTicketPart);
    BAIL_ON_VMKDC_ERROR(dwError);

    VMKDC_SAFE_FREE_DATA(pData);

    /*
     * Decrypt the authenticator
     */
    dwError = VmKdcDecryptEncData(pContext,
                                  pEncTicketPart->key,
                                  VMKDC_KU_TGS_REQ_AUTH,
                                  apReq->authenticator,
                                  &pData);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Decode the authenticator
     */
    dwError = VmKdcDecodeAuthenticator(pData, &pAuthenticator);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * Create a random session key
     */
    dwError = VmKdcRandomKey(pContext,
                             etype,
                             &pSessionKey);
    BAIL_ON_VMKDC_ERROR(dwError);

/*  RFC 4120, section 3.3.3, page 38
 *  By default, the address field, the client's name and realm, the list
 *  of transited realms, the time of initial authentication, the
 *  expiration time, and the authorization data of the newly-issued
 *  ticket will be copied from the TGT or renewable ticket.  If the
 *  transited field needs to be updated, but the transited type is not
 *  supported, the KDC_ERR_TRTYPE_NOSUPP error is returned.
 */

    kdc_time = time(NULL);
    if (!tgsRequest->req_body.from ||
        *tgsRequest->req_body.from < kdc_time ||
        (abs((long) (*tgsRequest->req_body.from - kdc_time)) <
         pContext->pGlobals->iClockSkew))
    {
        t_start = kdc_time;
    }
    else
    {
        dwError = ERROR_CANNOT_POSTDATE;
        BAIL_ON_VMKDC_ERROR(dwError);
    }

/*
 * If the request specifies an endtime, then the endtime of the new
 * ticket is set to the minimum of (a) that request, (b) the endtime
 * from the TGT, and (c) the starttime of the TGT plus the minimum of
 * the maximum life for the application server and the maximum life for
 * the local realm (the maximum life for the requesting principal was
 * already applied when the TGT was issued).  If the new ticket is to be
 * a renewal, then the endtime above is replaced by the minimum of (a)
 * the value of the renew_till field of the ticket and (b) the starttime
 * for the new ticket plus the life (endtime-starttime) of the old
 * ticket.
 */

    renew = VMKDC_FLAG_ISSET(tgsRequest->req_body.kdc_options, VMKDC_KO_RENEW);
    if (renew)
    {
        t_end = t_start + (pEncTicketPart->endtime - *pEncTicketPart->starttime);
        renew_till = pEncTicketPart->renew_till;
        if (*renew_till < t_end)
        {
            t_end = *renew_till;
        }
    }
    else
    {
        t_end = *pEncTicketPart->starttime + pContext->pGlobals->iMaxLife;
        t_reqTill = tgsRequest->req_body.till;
        if (t_reqTill && *t_reqTill != 0)
        {
            if (*t_reqTill < t_end)
            {
                t_end = *t_reqTill;
            }
            if (pEncTicketPart->endtime < t_end)
            {
                t_end = pEncTicketPart->endtime;
            }
        }
    }

    VMKDC_FLAG_SET(flags, VMKDC_TF_PRE_AUTHENT);
    if (VMKDC_FLAG_ISSET(tgsRequest->req_body.kdc_options, VMKDC_KO_RENEWABLE))
    {
        VMKDC_FLAG_SET(flags, VMKDC_TF_RENEWABLE);
    }
    if (VMKDC_FLAG_ISSET(tgsRequest->req_body.kdc_options, VMKDC_KO_FORWARDABLE))
    {
        VMKDC_FLAG_SET(flags, VMKDC_TF_FORWARDABLE);
    }
    if (VMKDC_FLAG_ISSET(tgsRequest->req_body.kdc_options, VMKDC_KO_PROXIABLE))
    {
        VMKDC_FLAG_SET(flags, VMKDC_TF_PROXIABLE);
    }

#if defined(VMDIR_ENABLE_PAC) || defined(WINJOIN_CHECK_ENABLED)
    /*
     * Get the krbtgt key
     */
    dwError = VmKdcSearchDirectory(
                      pContext,
                      apReq->ticket->sname,
                      &pKrbtgtEntry);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcFindKeyByEType(
                      pKrbtgtEntry,
                      etype,
                      &pKrbtgtKey);
    BAIL_ON_VMKDC_ERROR(dwError);

    if (!VmDirStringCompareA(
            VMKDC_GET_PTR_DATA(pEncTicketPart->cname->realm),
            VMKDC_GET_PTR_DATA(pSname->realm), TRUE))
    {
        /*
         * Create authorization data
         */
        dwError = VmKdcCreateAuthzData(
                          pContext,
                          pEncTicketPart->cname,
                          kdc_time,
                          pSKey,
                          pKrbtgtKey,
                          &pAuthzData);
        BAIL_ON_VMKDC_ERROR(dwError);
    }
#endif

    /*
     * Build a TICKET
     */
    dwError = VmKdcBuildTicket(
                      pContext,
                      pEncTicketPart->cname,
                      pSname,
                      pSKey,
                      pSessionKey,
                      flags,      /* flags */
                      NULL,       /* transited */
                      t_start,    /* authtime */
                      &t_start,   /* starttime */
                      t_end,      /* endtime */
                      renew_till, /* renew_till */
                      NULL,       /* caddr */
                      pAuthzData,
                      &pTicket);
    BAIL_ON_VMKDC_ERROR(dwError);

/*  RFC 4120, section 3.3.3, page 40
 *  The ciphertext part of the response in the KRB_TGS_REP message is
 *  encrypted in the sub-session key from the Authenticator, if present,
 *  or in the session key from the TGT.  It is not encrypted using the
 *  client's secret key.  Furthermore, the client's key's expiration date
 *  and the key version number fields are left out since these values are
 *  stored along with the client's database record, and that record is
 *  not needed to satisfy a request based on a TGT.
 */

    /*
     * Build a TGS-REP
     */
    dwError = VmKdcBuildTgsRep(
                      pContext,
                      pEncTicketPart->cname,
                      pSname,
                      pEncTicketPart->key,
                      pAuthenticator->subkey,
                      pSessionKey,
                      pTicket,
                      NULL,       /* last-req */
                      nonce,      /* nonce */
                      NULL,       /* key-expiration (optional) */
                      flags,      /* flags */
                      t_start,    /* authtime */
                      &t_start,   /* starttime */
                      t_end,      /* endtime */
                      renew_till, /* renew-till (optional) */
                      NULL,       /* caddr */
                      &pTgsRep);
    BAIL_ON_VMKDC_ERROR(dwError);

    /*
     * ASN.1 encode the TGS-REP
     */
    dwError = VmKdcEncodeTgsRep(pTgsRep, &krbMsg);
    BAIL_ON_VMKDC_ERROR(dwError);

    VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL,
            "Sending TGS-REP for server %s",
            pszServerName);

    *ppKrbMsg = krbMsg;

error:

    switch (dwError)
    {
        case 0: /* success, don't set error_code */
            break;
        case VMKDC_RPC_SERVER_NOTAVAIL:
            /*
             * Clear the global masterKey so it can't be used when vmdir is unavailable.
             */
            VmKdcTerminateDirectory(pContext->pGlobals);
            VmKdcdStateSet(VMKDCD_STARTUP);
            error_code = VMKDC_KDC_ERR_SVC_UNAVAILABLE;
            break;
        case ERROR_CANNOT_POSTDATE:
            error_code = VMKDC_KDC_ERR_CANNOT_POSTDATE;
            break;
        case ERROR_NO_PRINC:
        case ERROR_NO_KEY_ETYPE:
        default:
            error_code = VMKDC_KDC_ERR_C_PRINCIPAL_UNKNOWN;
            break;
    }
    if (tgsRequest && pSname && error_code)
    {
        dwError = VmKdcBuildKrbError(
                      tgsRequest->pvno,
                      NULL, /* ctime */
                      time(NULL), /* stime */
                      error_code, /* error_code */
                      NULL,
                      NULL,
                      VMKDC_GET_PTR_DATA(pSname->realm), /* realm */
                      pSname, /* sname */
                      NULL, /* e_text */
                      NULL, /* e_data */
                      &krbMsg);
        if (dwError == 0)
        {
            *ppKrbMsg = krbMsg;

            VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL,
                     "Sending KRB-ERROR %d for server %s",
                     error_code, pszServerName);
        }
        else
        {
            VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL,
                     "Failed to build KRB-ERROR %d for server %s",
                     error_code, pszServerName);
        }
    }
    else if (dwError)
    {
        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL,
                 "VmKdcProcessTgsRequest failed, error code: (%u)",
                 dwError);
    }
    VMKDC_SAFE_FREE_DIRECTORY_ENTRY(pDirectoryEntry);
    VMKDC_SAFE_FREE_DIRECTORY_ENTRY(pServerEntry);
    VMKDC_SAFE_FREE_TGSREQ(tgsRequest);
    VMKDC_SAFE_FREE_TGSREP(pTgsRep);
    VMKDC_SAFE_FREE_APREQ(apReq);
    VMKDC_SAFE_FREE_ENCTICKETPART(pEncTicketPart);
    VMKDC_SAFE_FREE_AUTHENTICATOR(pAuthenticator);
    VMKDC_SAFE_FREE_DATA(pData);
    VMKDC_SAFE_FREE_KEY(pSessionKey);
    VMKDC_SAFE_FREE_TICKET(pTicket);
    VMKDC_SAFE_FREE_DATA(pAsnData);
    VMKDC_SAFE_FREE_STRINGA(pszServerName);
#if defined(VMDIR_ENABLE_PAC) || defined(WINJOIN_CHECK_ENABLED)
    VMKDC_SAFE_FREE_AUTHZDATA(pAuthzData);
    VMKDC_SAFE_FREE_DIRECTORY_ENTRY(pKrbtgtEntry);
#endif
    return dwError;
}

static
BOOLEAN
isCrossRealmTGT(
    PVMKDC_PRINCIPAL pPrincipal)
{
    BOOLEAN bRetval = FALSE;

    if (pPrincipal->numComponents == 2
        && !VmDirStringCompareA(VMKDC_GET_PTR_DATA(pPrincipal->components[0]),
                                "krbtgt", TRUE)
        &&  VmDirStringCompareA(VMKDC_GET_PTR_DATA(pPrincipal->components[1]),
                                VMKDC_GET_PTR_DATA(pPrincipal->realm), TRUE))
    {
        bRetval = TRUE;
    }

    return bRetval;
}

static
DWORD
VmDirGetClosestServer(
    PVMKDC_CONTEXT pContext,
    PVMKDC_PRINCIPAL pServer,
    PVMKDC_PRINCIPAL *ppClosestServer)
{
    DWORD dwError = 0;
    PVMKDC_PRINCIPAL pClosestServer = NULL;
    PSTR pszDomain = NULL;
    PSTR pszRequestedDomain = NULL;
    PSTR pszClosestDomain = NULL;
    PSTR pszUPN = NULL;
    PVMDIR_STRING_LIST pDomainStringList = NULL;
    PVMDIR_STRING_LIST pRequestedDomainStringList = NULL;
    PSTR r1 = NULL;
    PSTR r2 = NULL;
    typedef enum {
        TRUST_NONE,
        TRUST_SELF,
        TRUST_PARENT,
        TRUST_CHILD,
    } TRUST_KIND;
    TRUST_KIND eTrustKind = TRUST_NONE;
    DWORD dwIndex = 0;
    DWORD dwNumToRemove = 0;

    pszDomain = VMKDC_GET_PTR_DATA(pServer->realm);
    pszRequestedDomain = VMKDC_GET_PTR_DATA(pServer->components[1]);

    /*
     * If the requested domain is the same as the domain, return it.
     */
    if (!VmDirStringCompareA(pszRequestedDomain, pszDomain, TRUE))
    {
        eTrustKind = TRUST_SELF;
    }
    else
    {
        dwError = VmDirStringToTokenList(
                      pszDomain,
                      ".",
                      &pDomainStringList);
        BAIL_ON_VMDIR_ERROR(dwError);

        dwError = VmDirStringListReverse(
                      pDomainStringList);
        BAIL_ON_VMDIR_ERROR(dwError);

        dwError = VmDirStringToTokenList(
                      pszRequestedDomain,
                      ".",
                      &pRequestedDomainStringList);
        BAIL_ON_VMDIR_ERROR(dwError);

        dwError = VmDirStringListReverse(
                      pRequestedDomainStringList);
        BAIL_ON_VMDIR_ERROR(dwError);

        if (!pDomainStringList
            || !pDomainStringList->pStringList[0]
            || !pRequestedDomainStringList
            || !pRequestedDomainStringList->pStringList[0]
            || (VmDirStringCompareA(pDomainStringList->pStringList[0],
                                    pRequestedDomainStringList->pStringList[0], FALSE) != 0))
        {
            /* fail if they do not share the same root */
            eTrustKind = TRUST_NONE;
        }
        else
        {
            if (pDomainStringList->dwCount == pRequestedDomainStringList->dwCount)
            {
                eTrustKind = TRUST_PARENT;
            }
            else if (pDomainStringList->dwCount < pRequestedDomainStringList->dwCount)
            {
                dwError = VmDirTokenListToString(
                              pDomainStringList,
                              ".",
                              &r1);
                BAIL_ON_VMKDC_ERROR(dwError);

                dwError = VmDirTokenListToString(
                              pRequestedDomainStringList,
                              ".",
                              &r2);
                BAIL_ON_VMKDC_ERROR(dwError);

                if (!VmDirStringNCompareA(r1, r2, VmDirStringLenA(r1), FALSE))
                {
                    eTrustKind = TRUST_CHILD;
                }
                else
                {
                    eTrustKind = TRUST_PARENT;
                }
            }
            else
            {
                eTrustKind = TRUST_PARENT;
            }
        }
    }

    switch (eTrustKind)
    {
    case TRUST_SELF:
        dwError = VmKdcAllocateStringA(pszDomain, &pszClosestDomain);
        BAIL_ON_VMKDC_ERROR(dwError);
        break;

    case TRUST_PARENT:
        dwError = VmDirStringListReverse(
                      pDomainStringList);
        BAIL_ON_VMDIR_ERROR(dwError);

        dwError = VmDirStringListRemove(
                      pDomainStringList,
                      pDomainStringList->pStringList[0]);
        BAIL_ON_VMKDC_ERROR(dwError);

        dwError = VmDirTokenListToString(
                      pDomainStringList,
                      ".",
                      &pszClosestDomain);
        BAIL_ON_VMKDC_ERROR(dwError);
        break;

    case TRUST_CHILD:
        dwError = VmDirStringListReverse(
                      pRequestedDomainStringList);
        BAIL_ON_VMDIR_ERROR(dwError);

        dwNumToRemove = pRequestedDomainStringList->dwCount -
                        pDomainStringList->dwCount - 1;

        for (dwIndex = 0; dwIndex < dwNumToRemove; dwIndex++)
        {
            dwError = VmDirStringListRemove(
                          pRequestedDomainStringList,
                          pRequestedDomainStringList->pStringList[0]);
            BAIL_ON_VMKDC_ERROR(dwError);
        }

        dwError = VmDirTokenListToString(
                      pRequestedDomainStringList,
                      ".",
                      &pszClosestDomain);
        BAIL_ON_VMKDC_ERROR(dwError);
        break;

    case TRUST_NONE:
    default:
        dwError = ERROR_INVALID_PARAMETER;
        BAIL_ON_VMKDC_ERROR(dwError);
    }

    dwError = VmKdcAllocateStringPrintf(
                  &pszUPN,
                  "krbtgt/%s@%s",
                  pszClosestDomain,
                  pszDomain);
    BAIL_ON_VMKDC_ERROR(dwError);

    dwError = VmKdcParsePrincipalName(pContext, pszUPN, &pClosestServer);
    BAIL_ON_VMKDC_ERROR(dwError);

    *ppClosestServer = pClosestServer;

cleanup:
    VMDIR_SAFE_FREE_STRINGA(r1);
    VMDIR_SAFE_FREE_STRINGA(r2);
    if (pDomainStringList)
    {
        VmDirStringListFree(pDomainStringList);
    }
    if (pRequestedDomainStringList)
    {
        VmDirStringListFree(pRequestedDomainStringList);
    }
    return dwError;

error:

    goto cleanup;
}
