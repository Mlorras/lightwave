#!/bin/bash -e

###
# Initialize common script
init()
{
  if [[ -z ${COMMON_INIT} ]]
  then
    source $(dirname $(realpath $0))/aws.sh
    source $(dirname $(realpath $0))/aws_dns.sh
    source $(dirname $(realpath $0))/promote.sh
    source $(dirname $(realpath $0))/healthcheck.sh
    export COMMON_INIT=true
  fi
}

###
# Load all configuration variables
load_variables()
{
  VARFILE=$(dirname $(realpath $0))/variables
  if [[ -f ${VARFILE} ]]
  then
    echo "Loading variables from file ${VARFILE}"
    source ${VARFILE}

  else
    echo "Loading variables from cloud"
    load_aws_variables

    # check mandatory variables
    [[ ${ROOTDIR} ]]
    [[ ${SAN_ENTRY} ]]
    [[ ${SITE} ]]

    export HOSTNAME=$(hostname -f)
    echo "HOSTNAME=${HOSTNAME}" >> ${VARFILE}
    echo "HOSTNAME=${HOSTNAME}"
    [[ ${HOSTNAME} ]]

    export CONFIGDIR=${ROOTDIR}/configs
    echo "CONFIGDIR=${CONFIGDIR}" >> ${VARFILE}
    echo "CONFIGDIR=${CONFIGDIR}"
    [[ ${CONFIGDIR} ]]

    export CRONDIR=${ROOTDIR}/crontab
    echo "CRONDIR=${CRONDIR}" >> ${VARFILE}
    echo "CRONDIR=${CRONDIR}"
    [[ ${CRONDIR} ]]

    export PUB_DNS=8.8.8.8
    echo "PUB_DNS=${PUB_DNS}" >> ${VARFILE}
    echo "PUB_DNS=${PUB_DNS}"
    [[ ${PUB_DNS} ]]
  fi
}

###
# Load all user credentials
load_credentials()
{
  export ADMINISTRATOR_USER='Administrator'
  export DOMAIN_PROMOTER_USER='cascade-promote'
  export DOMAIN_JOINER_USER='cascade-join'
  export SRV_ACCT_MGR_USER='srv-acct-mgr'
  export SCHEMA_MGR_USER='schema-mgr'

  load_aws_credentials

  # check mandatory credentials
  [[ ${DOMAIN_PROMOTER_PASS} ]]
  [[ ${DOMAIN_JOINER_PASS} ]]
  [[ ${SRV_ACCT_MGR_PASS} ]]
  [[ ${SCHEMA_MGR_PASS} ]]
}

###
# Stop all lightwave services
stop_services()
{
  systemctl stop vmware-stsd
  /opt/likewise/bin/lwsm stop vmdns
  /opt/likewise/bin/lwsm stop vmdir
  /opt/likewise/bin/lwsm stop vmca
  /opt/likewise/bin/lwsm stop vmafd
}

###
# Start all lightwave services
start_services()
{
  /opt/likewise/bin/lwsm start vmafd
  /opt/likewise/bin/lwsm start vmca
  /opt/likewise/bin/lwsm start vmdir
  /opt/likewise/bin/lwsm start vmdns
  systemctl daemon-reload
  systemctl start vmware-stsd
}

###
# Read a likewise registry value
read_registry_val()
{
  KEYNAME=$1
  VALNAME=$2
  echo $(/opt/likewise/bin/lwregshell list_values ${KEYNAME} | grep -i ${VALNAME} || true)
}

###
# Add a new likewise registry value - noop if already exists
add_registry_val()
{
  KEYNAME=$1
  VALNAME=$2
  VALTYPE=$3
  VALUE=$4

  if [[ -z $(read_registry_val ${KEYNAME} ${VALNAME}) ]]
  then
    /opt/likewise/bin/lwregshell add_value ${KEYNAME} ${VALNAME} ${VALTYPE} ${VALUE}
  fi
}

###
# Replace a likewise registry value - override if already exists
replace_registry_val()
{
  KEYNAME=$1
  VALNAME=$2
  VALTYPE=$3
  VALUE=$4

  if [[ -z $(read_registry_val ${KEYNAME} ${VALNAME}) ]]
  then
    /opt/likewise/bin/lwregshell add_value ${KEYNAME} ${VALNAME} ${VALTYPE} ${VALUE}
  else
    /opt/likewise/bin/lwregshell set_value ${KEYNAME} ${VALNAME} ${VALUE}
  fi
}

###
# Starts any new cronjobs
start_cron()
{
  cp ${CRONDIR}/monitor-core-dump.sh /opt/vmware/share/config/monitor-core-dump.sh

  if [[ -z `pgrep crond` ]]
  then
    echo "crond is not running, starting crond"
    systemctl start crond
  fi

  crontab ${CRONDIR}/lw-cron.txt
  crontab -l
}

###
# Concatenates elements of an array using the specified separator
string_join()
{
  local IFS="$1"
  shift
  echo "$*"
}

init