/*
 * Copyright © 2018 VMware, Inc.  All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the “License”) you may not
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

DWORD
LwCAPluginLoad(
   PLWCA_DB_FUNCTION_TABLE pFt
   )
{
    DWORD dwError = 0;

    if (!pFt)
    {
        dwError = ERROR_INVALID_PARAMETER;
        BAIL_ON_TEST_PLUGIN_ERROR(dwError);
    }

    pFt->pFnInit = &LwCADbTestPluginInitialize;
    pFt->pFnAddCA = &LwCADbTestPluginAddCA;
    pFt->pFnAddCertData = &LwCADbTestPluginAddCertData;
    pFt->pFnCheckCA = &LwCADbTestPluginCheckCA;
    pFt->pFnCheckCertData = &LwCADbTestPluginCheckCertData;
    pFt->pFnGetCA = &LwCADbTestPluginGetCA;
    pFt->pFnGetCACertificates = &LwCADbTestPluginGetCACertificates;
    pFt->pFnGetCertData = &LwCADbTestPluginGetCertData;
    pFt->pFnGetCACRLNumber = &LwCADbTestPluginGetCACRLNumber;
    pFt->pFnGetParentCAId = &LwCADbTestPluginGetParentCAId;
    pFt->pFnGetCAStatus = &LwCADbTestPluginGetCAStatus;
    pFt->pFnGetCAAuthBlob = &LwCADbTestPluginGetCAAuthBlob;
    pFt->pFnUpdateCA = &LwCADbTestPluginUpdateCA;
    pFt->pFnUpdateCAStatus = &LwCADbTestPluginUpdateCAStatus;
    pFt->pFnUpdateCertData = &LwCADbTestPluginUpdateCertData;
    pFt->pFnLockCA = &LwCADbTestPluginLockCA;
    pFt->pFnUnlockCA = &LwCADbTestPluginUnlockCA;
    pFt->pFnLockCert = &LwCADbTestPluginLockCert;
    pFt->pFnUnlockCert = &LwCADbTestPluginUnlockCert;
    pFt->pFnUpdateCACRLNumber = &LwCADbTestPluginUpdateCACRLNumber;
    pFt->pFnFreeCAData = &LwCADbTestPluginFreeCAData;
    pFt->pFnFreeCertDataArray = &LwCADbTestPluginFreeCertDataArray;
    pFt->pFnFreeCertArray = &LwCADbTestPluginFreeCertificates;
    pFt->pFnFreeString = &LwCADbTestPluginFreeString;
    pFt->pFnFreeHandle = &LwCADbTestPluginFreeHandle;

cleanup:
    return dwError;

error:
    goto cleanup;
}

VOID
LwCAPluginUnload(
   VOID
   )
{
}


DWORD
LwCADbTestPluginInitialize(
    PCSTR           pcszPluginConfigPath,
    PLWCA_DB_HANDLE *ppHandle
    )
{
    return 0;
}

DWORD
LwCADbTestPluginAddCA(
    PLWCA_DB_HANDLE         pHandle,
    PCSTR                   pcszCAId,
    PLWCA_DB_CA_DATA        pCAData,
    PCSTR                   pcszParentCAId
    )
{
    return 0;
}

DWORD
LwCADbTestPluginAddCertData(
    PLWCA_DB_HANDLE         pHandle,
    PCSTR                   pcszCAId,
    PLWCA_DB_CERT_DATA      pCertData
    )
{
    return 0;
}

DWORD
LwCADbTestPluginCheckCA(
    PLWCA_DB_HANDLE        pHandle,
    PCSTR                  pcszCAId,
    PBOOLEAN               pbExists
    )
{
    return 0;
}

DWORD
LwCADbTestPluginCheckCertData(
    PLWCA_DB_HANDLE         pHandle,
    PCSTR                   pcszCAId,
    PCSTR                   pcszSerialNumber,
    PBOOLEAN                pbExists
    )
{
    return 0;
}

DWORD
LwCADbTestPluginGetCA(
    PLWCA_DB_HANDLE         pHandle,
    PCSTR                   pcszCAId,
    PLWCA_DB_CA_DATA        *ppCAData
    )
{
    return 0;
}

DWORD
LwCADbTestPluginGetCACertificates(
    PLWCA_DB_HANDLE              pHandle,
    PCSTR                        pcszCAId,
    PLWCA_CERTIFICATE_ARRAY      *ppCertArray
    )
{
    return 0;
}

DWORD
LwCADbTestPluginGetCertData(
    PLWCA_DB_HANDLE             pHandle,
    PCSTR                       pcszCAId,
    PLWCA_DB_CERT_DATA_ARRAY    *ppCertDataArray
    )
{
    return 0;
}

DWORD
LwCADbTestPluginGetCACRLNumber(
    PLWCA_DB_HANDLE             pHandle,
    PCSTR                       pcszCAId,
    PSTR                        *ppszCRLNumber
    )
{
    return 0;
}

DWORD
LwCADbTestPluginUpdateCA(
    PLWCA_DB_HANDLE         pHandle,
    PCSTR                   pcszCAId,
    PLWCA_DB_CA_DATA        pCAData
    )
{
    return 0;
}


DWORD
LwCADbTestPluginUpdateCAStatus(
    PLWCA_DB_HANDLE         pHandle,
    PCSTR                   pcszCAId,
    LWCA_CA_STATUS          status
    )
{
    return 0;
}

DWORD
LwCADbTestPluginLockCA(
    PLWCA_DB_HANDLE pHandle,
    PCSTR           pcszCAId,
    PSTR            *ppszUuid
    )
{
    return 0;
}

DWORD
LwCADbTestPluginUnlockCA(
    PLWCA_DB_HANDLE pHandle,
    PCSTR           pcszCAId,
    PCSTR           pcszUuid
    )
{
    return 0;
}

DWORD
LwCADbTestPluginLockCert(
    PLWCA_DB_HANDLE pHandle,
    PCSTR           pcszCAId,
    PCSTR           pcszSerialNumber,
    PSTR            *ppszUuid
    )
{
    return 0;
}

DWORD
LwCADbTestPluginUnlockCert(
    PLWCA_DB_HANDLE pHandle,
    PCSTR           pcszCAId,
    PCSTR           pcszSerialNumber,
    PCSTR           pcszUuid
    )
{
    return 0;
}

DWORD
LwCADbTestPluginUpdateCertData(
    PLWCA_DB_HANDLE         pHandle,
    PCSTR                   pcszCAId,
    PLWCA_DB_CERT_DATA      pCertData
    )
{
    return 0;
}

DWORD
LwCADbTestPluginUpdateCACRLNumber(
    PLWCA_DB_HANDLE             pHandle,
    PCSTR                       pcszCAId,
    PCSTR                       pcszCRLNumber
    )
{
    return 0;
}

DWORD
LwCADbTestPluginGetParentCAId(
    PLWCA_DB_HANDLE             pHandle,
    PCSTR                       pcszCAId,
    PSTR                        *ppszParentCAId
    )
{
    return 0;
}

DWORD
LwCADbTestPluginGetCAStatus(
    PLWCA_DB_HANDLE             pHandle,
    PCSTR                       pcszCAId,
    PLWCA_CA_STATUS             pStatus
    )
{
    return 0;
}

DWORD
LwCADbTestPluginGetCAAuthBlob(
    PLWCA_DB_HANDLE             pHandle,
    PCSTR                       pcszCAId,
    PSTR                        *ppszAuthBlob
    )
{
    return 0;
}

VOID
LwCADbTestPluginFreeCAData(
    PLWCA_DB_CA_DATA pCAData
    )
{
}

VOID
LwCADbTestPluginFreeCertDataArray(
    PLWCA_DB_CERT_DATA_ARRAY pCertDataArray
    )
{
}

VOID
LwCADbTestPluginFreeCertificates(
    PLWCA_CERTIFICATE_ARRAY pCertArray
    )
{
}

VOID
LwCADbTestPluginFreeString(
    PSTR  pszString
    )
{
}

VOID
LwCADbTestPluginFreeHandle(
    PLWCA_DB_HANDLE pDbHandle
    )
{
}
