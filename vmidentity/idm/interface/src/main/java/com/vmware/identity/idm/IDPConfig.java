/*
 *
 *  Copyright (c) 2012-2015 VMware, Inc.  All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may not
 *  use this file except in compliance with the License.  You may obtain a copy
 *  of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, without
 *  warranties or conditions of any kind, EITHER EXPRESS OR IMPLIED.  See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 *
 */

package com.vmware.identity.idm;

import java.io.Serializable;
import java.security.GeneralSecurityException;
import java.security.PublicKey;
import java.security.cert.CertPathBuilder;
import java.security.cert.CertPathBuilderException;
import java.security.cert.CertPathBuilderResult;
import java.security.cert.CertStore;
import java.security.cert.CollectionCertStoreParameters;
import java.security.cert.PKIXBuilderParameters;
import java.security.cert.TrustAnchor;
import java.security.cert.X509CertSelector;
import java.security.cert.X509Certificate;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.apache.commons.lang.Validate;

/**
 * @author billli
 *
 */
public class IDPConfig implements Serializable
{
    private static final long serialVersionUID = -6683353323177245758L;

    public static final String IDP_PROTOCOL_SAML_2_0 = "urn:oasis:names:tc:SAML:2.0:protocol";
    public static final String IDP_PROTOCOL_OAUTH_2_0 = "urn:oasis:names:tc:OAUTH:2.0:protocol";

    final String entityID;
    final String protocol;
    String alias;
    Collection<String> nameIDFormats;
    Collection<ServiceEndpoint> ssoServices;
    Collection<ServiceEndpoint> sloServices;
    List<X509Certificate> signingCertificateChain;
    PublicKey publicKey;
    AttributeConfig[] subjectFormatMappings;
    Map<TokenClaimAttribute,List<String>> tokenClaimGroupMappings;
    boolean isJitEnabled;
    String upnSuffix;
    boolean isMultiTenant;
    OidcConfig oidcConfig;

    /**
     *
     * @param aEntityId
     *            Cannot be null or empty
     */
    public IDPConfig(String aEntityId)
    {
        this(aEntityId, IDPConfig.IDP_PROTOCOL_SAML_2_0);
    }

    /**
     *
     * @param aEntityId
     *            Cannot be null or empty
     * @param protocol
     *            must be one
     */
    public IDPConfig(String aEntityId, String protocol)
    {
        this(aEntityId, protocol, null);
    }

    /**
     *
     * @param aEntityId
     *            Cannot be null or empty
     * @param protocol
     *            must be one
     * @param oidcConfig
     *
     */
    public IDPConfig(String aEntityId, String protocol, OidcConfig oidcConfig)
    {
        Validate.notEmpty(aEntityId);
        validateProtocol(protocol);
        entityID = aEntityId;
        this.protocol = protocol;
        this.oidcConfig = oidcConfig != null ? oidcConfig : new OidcConfig();
    }


    /**
     *
     * @return protocol
     *         The Protocol is one of {websso, oidc...}
     */
    public String getProtocol()
    {
        return protocol;
    }

    /**
     *
     * @return entityID
     */
    public String getEntityID()
    {
        return entityID;
    }

    /**
     *
     * @return alias of the external Idp
     */
    public String getAlias()
    {
        return alias;
    }


    /**
     *
     * @param alias can be null, but otherwise cannot be empty
     */
    public void setAlias(String alias)
    {
        if (alias != null)
            Validate.notEmpty(alias);
        this.alias = alias;
    }

    public boolean getJitAttribute()
    {
        return isJitEnabled;
    }

    public void setJitAttribute(boolean enableJit)
    {
        this.isJitEnabled = enableJit;
    }

    /**
     * @return the upn suffix for the idp
     */
    public String getUpnSuffix()
    {
        return this.upnSuffix;
    }

    /**
     * set the upn suffix for the idp
     * @param upnSuffix can be null, but otherwise cannot be empty
     */
    public void setUpnSuffix(String upnSuffix)
    {
        if (upnSuffix != null) {
            Validate.notEmpty(upnSuffix);
        }
        this.upnSuffix = upnSuffix;
    }

    public boolean isMultiTenant() {
        return this.isMultiTenant;
    }

    public void setMultiTenant(boolean isMultiTenant) {
        this.isMultiTenant = isMultiTenant;
    }

    /**
     *
     * @return the signing certificate chain.
     */
    public List<X509Certificate> getSigningCertificateChain()
    {
        return signingCertificateChain;
    }

    /**
     * set the signing certificate chain
     *
     * @param signingCertificateChain
     *            cannot be null or empty, must be a valid certificate chain
     *            with user's signing certificate first and root CA certificate
     *            last, and the chain consists of all the certificates in the
     *            list.
     * @throws ExternalIDPExtraneousCertsInCertChainException
     *             when extraneous certificates not belong to the chain found in
     *             the list
     * @throws ExternalIDPCertChainInvalidTrustedPathException
     *             when there is no trusted path found anchored at the root CA
     *             certificate.
     */
    public void setSigningCertificateChain(List<X509Certificate> signingCertificateChain)
            throws ExternalIDPExtraneousCertsInCertChainException,
            ExternalIDPCertChainInvalidTrustedPathException

    {
        Validate.notEmpty(signingCertificateChain);
        validateSingleX509CertChain(signingCertificateChain);
        this.signingCertificateChain = signingCertificateChain;
    }

    /**
     *
     * @return the public key used to sign tokens
     */
    public PublicKey getPublicKey() { return publicKey; }

    /**
     *
     * @param publicKey The public key used to sign the tokens
     */
    public void setPublicKey(PublicKey publicKey) {
        Validate.notNull(publicKey);
        this.publicKey = publicKey;
    }

    /**
     *
     * @return list of name id formats
     */
    public Collection<String> getNameIDFormats()
    {
        return nameIDFormats;
    }

    /**
     *
     * @param nameIDFormats cannot be null or empty
     */
    public void setNameIDFormats(Collection<String> nameIDFormats)
    {
        this.nameIDFormats = nameIDFormats;
    }

    /**
     *
     * @return list of SSO service objects
     */
    public Collection<ServiceEndpoint> getSsoServices()
    {
        return ssoServices;
    }

    /**
     *
     * @param ssoServices cannot be null or empty
     */
    public void setSsoServices(Collection<ServiceEndpoint> ssoServices)
    {
        this.ssoServices = ssoServices;
    }

    /**
     *
     * @return list of SLO service objects
     */
    public Collection<ServiceEndpoint> getSloServices()
    {
        return sloServices;
    }

    /**
     *
     * @param sloServices cannot be null or empty
     */
    public void setSloServices(Collection<ServiceEndpoint> sloServices)
    {
        this.sloServices = sloServices;
    }

   /**
    * @ return array of attribute configuration for subject format
    */
   public AttributeConfig[] getSubjectFormatMappings()
   {
      return subjectFormatMappings;
   }

   /**
    * @param subjectFormatMappings
    *           can be null or empty.
    */
   public void setSubjectFormatMappings(AttributeConfig[] subjectFormatMappings)
   {
      this.subjectFormatMappings = subjectFormatMappings;
   }

   /**
    * @ return array of mappings for claim to group
    */
   public Map<TokenClaimAttribute,List<String>> getTokenClaimGroupMappings()
   {
      return tokenClaimGroupMappings;
   }

   /**
    * @param tokenClaimGroupMappings
    *           Cannot be null or empty; the collection of group sid lists cannot contain null.
    */
   public void setTokenClaimGroupMappings(Map<TokenClaimAttribute,List<String>> tokenClaimGroupMappings)
   {
      Validate.notNull(tokenClaimGroupMappings, "Token claim group mappings is null.");
      Validate.noNullElements(tokenClaimGroupMappings.values(), "Token claim group mappings contains null group name list.");
      this.tokenClaimGroupMappings = tokenClaimGroupMappings;
   }

    public OidcConfig getOidcConfig() {
        return oidcConfig;
    }

    public void setOidcConfig(OidcConfig oidcConfig) {
        this.oidcConfig = oidcConfig;
    }

    /**
     *
     * @param urlStr
     *            cannot be null or empty
     * @return true if one end-point location of the SSO / SLO services matches
     *         the specified URL. otherwise false.
     */
    public boolean isMatchingUrl(String urlStr)
    {
        Validate.notEmpty(urlStr);
        for (ServiceEndpoint sso : ssoServices)
        {
            if (sso.getEndpoint().equals(urlStr))
            {
                return true;
            }
        }
        for (ServiceEndpoint slo : sloServices)
        {
            if (slo.getEndpoint().equals(urlStr))
            {
                return true;
            }
        }
        return false;
    }

    /**
     *
     * @param protocol
     * @return true if protocol matches one of
     *  "urn:oasis:names:tc:SAML:2.0:protocol",
     *  "urn:oasis:names:tc:OAUTH:2.0:protocol
     */
    public static void validateProtocol(String protocol)
    {
        if (protocol == null || protocol.isEmpty() ||
                (!protocol.equals(IDPConfig.IDP_PROTOCOL_OAUTH_2_0) &&
                        !protocol.equals(IDPConfig.IDP_PROTOCOL_SAML_2_0)))
        {
            throw new RuntimeException("Error: Invalid value specified for protocol in IDP Config");
        }
    }

    /**
     * Validate the chain is in the required order user's certificate first,
     * root CA certificate last including the case of only root CA is present.
     * Also validate that there is only one chain, which consists of all the
     * certificates listed.
     */
    private static boolean validateSingleX509CertChain(
            List<X509Certificate> chain)
            throws ExternalIDPExtraneousCertsInCertChainException,
            ExternalIDPCertChainInvalidTrustedPathException
    {
        final String ALGO_PKIX = "PKIX"; //for X.509

        final String CERTSTORE_PROVIDER_COLLECTION = "Collection";

        try
        {
            Set<TrustAnchor> anchors = new HashSet<TrustAnchor>();
            anchors.add(new TrustAnchor(chain.get(chain.size() - 1), null));

            X509CertSelector targetCertSelector = new X509CertSelector();
            targetCertSelector.setCertificate(chain.get(0));

            CertStore builderStore =
                    CertStore.getInstance(CERTSTORE_PROVIDER_COLLECTION,
                            new CollectionCertStoreParameters(chain));

            PKIXBuilderParameters buildParams =
                    new PKIXBuilderParameters(anchors, targetCertSelector);
            buildParams.addCertStore(builderStore);
            buildParams.setRevocationEnabled(false);

            CertPathBuilder pathBuilder =
                    CertPathBuilder.getInstance(ALGO_PKIX);
            CertPathBuilderResult builderResult =
                    pathBuilder.build(buildParams);

            if (chain.size() - 1 != builderResult.getCertPath().getCertificates().size())
            {
                throw new ExternalIDPExtraneousCertsInCertChainException(chain);
            }
            return true;

        } catch (CertPathBuilderException cpbe)
        {
            throw new ExternalIDPCertChainInvalidTrustedPathException(cpbe.getMessage(), chain); // no need to chain the exception.
        } catch (GeneralSecurityException gse)
        {
            throw new ExternalIDPCertChainInvalidTrustedPathException(gse.getMessage(), chain);
        }
    }
}
