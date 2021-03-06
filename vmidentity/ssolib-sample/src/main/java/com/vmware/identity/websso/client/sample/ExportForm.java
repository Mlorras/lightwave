/*
 *  Copyright (c) 2012-2018 VMware, Inc.  All Rights Reserved.
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
 */
package com.vmware.identity.websso.client.sample;

/**
 * Form used to import metadata
 *
 */
public class ExportForm {

	private String metadata;
	private String message;
	private String identityProviderFQDN;

	public ExportForm() {
		setMetadata("");
		setMessage("");
		setIdentityProviderFQDN("");
	}

	/**
	 * @return the metadata
	 */
	public String getMetadata() {
		return metadata;
	}

	/**
	 * @param metadata
	 *            the metadata to set
	 */
	public void setMetadata(String metadata) {
		this.metadata = metadata;
	}

	/**
	 * @return the message
	 */
	public String getMessage() {
		return message;
	}

	/**
	 * @param message
	 *            the message to set
	 */
	public void setMessage(String message) {
		this.message = message;
	}

	/**
	 *
	 * @return idp FQDN
	 */
	public String getIdentityProviderFQDN() {
		return identityProviderFQDN;
	}

	/**
	 *
	 * @param identityProviderFQDN
	 */
	public void setIdentityProviderFQDN(String identityProviderFQDN) {
		this.identityProviderFQDN = identityProviderFQDN;
	}
}
