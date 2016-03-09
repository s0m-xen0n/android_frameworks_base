/*
 * Copyright 2008, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "NetUtils"

#include "jni.h"
#include "JNIHelp.h"
#include "NetdClient.h"
#include "resolv_netid.h"
#include <utils/misc.h>
#include <android_runtime/AndroidRuntime.h>
#include <utils/Log.h>
#include <arpa/inet.h>
#include <cutils/properties.h>
#ifdef MTK_HARDWARE
#include <fcntl.h>
#endif

extern "C" {
int ifc_enable(const char *ifname);
int ifc_disable(const char *ifname);
int ifc_reset_connections(const char *ifname, int reset_mask);

int dhcp_do_request(const char * const ifname,
                    const char *ipaddr,
                    const char *gateway,
                    uint32_t *prefixLength,
                    const char *dns[],
                    const char *server,
                    uint32_t *lease,
                    const char *vendorInfo,
                    const char *domains,
                    const char *mtu);

int dhcp_do_request_renew(const char * const ifname,
                    const char *ipaddr,
                    const char *gateway,
                    uint32_t *prefixLength,
                    const char *dns[],
                    const char *server,
                    uint32_t *lease,
                    const char *vendorInfo,
                    const char *domains,
                    const char *mtu);

int dhcp_stop(const char *ifname);
int dhcp_release_lease(const char *ifname);
char *dhcp_get_errmsg();

#if 0  // #ifdef MTK_HARDWARE
int ifc_reset_connection_by_uid(int uid, int err);

int PPPOE_stop(const char *interface);

int PPPOE_do_request(const char *interface, int timeout_sec, const char *usr, const char *passwd, int interval, int failure, int mtu, int mru, int mss, char* iplocal, char* ipremote, char* gateway, char* dns1, char* dns2, char * ppplinkname);

char *PPPOE_get_errmsg();
/*mtk_net pcscf*/
int dhcp_do_sip_request(const char *iface);
int dhcp_stop_sip_request(const char *iface);
int dhcpv6_do_sip_request(const char *iface);
int dhcpv6_stop_sip_request(const char *iface);
/*mtk_net pcscf end*/

char *dhcpv6_get_errmsg();

/*add dhcpv6 corresponding c method declaration*/
int dhcpv6_do_request(const char *interface, char *ipaddr,
        char *dns1,
        char *dns2,
        uint32_t *lease, uint32_t *pid);
int dhcpv6_stop(const char *interface);
int dhcpv6_do_request_renew(const char *interface, const int pid, char *ipaddr,
        char *dns1,
        char *dns2,
        uint32_t *lease);
int dhcpv6_PD_request(const char *interface, char *prefix, uint32_t *lease);
int dhcpv6_PD_renew(const char *interface, char *prefix, uint32_t *lease);
int dhcpv6_PD_stop(const char *interface);
char *PD_get_errmsg();
#endif
}

#define NETUTILS_PKG_NAME "android/net/NetworkUtils"

namespace android {

/*
 * The following remembers the jfieldID's of the fields
 * of the DhcpInfo Java object, so that we don't have
 * to look them up every time.
 */
static struct fieldIds {
    jmethodID clear;
    jmethodID setIpAddress;
    jmethodID setGateway;
    jmethodID addDns;
    jmethodID setDomains;
    jmethodID setServerAddress;
    jmethodID setLeaseDuration;
    jmethodID setVendorInfo;
#if 0  // #ifdef MTK_HARDWARE
    jmethodID setPidForRenew;
    jmethodID setPpplinkname;
#endif
} dhcpResultsFieldIds;

static jint android_net_utils_resetConnections(JNIEnv* env, jobject clazz,
      jstring ifname, jint mask)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);

    ALOGD("android_net_utils_resetConnections in env=%p clazz=%p iface=%s mask=0x%x\n",
          env, clazz, nameStr, mask);

    result = ::ifc_reset_connections(nameStr, mask);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jint)result;
}

static jboolean android_net_utils_runDhcpCommon(JNIEnv* env, jobject clazz, jstring ifname,
        jobject dhcpResults, bool renew)
{
    int result;
    char  ipaddr[PROPERTY_VALUE_MAX];
    uint32_t prefixLength;
    char gateway[PROPERTY_VALUE_MAX];
    char    dns1[PROPERTY_VALUE_MAX];
    char    dns2[PROPERTY_VALUE_MAX];
    char    dns3[PROPERTY_VALUE_MAX];
    char    dns4[PROPERTY_VALUE_MAX];
    const char *dns[5] = {dns1, dns2, dns3, dns4, NULL};
    char  server[PROPERTY_VALUE_MAX];
    uint32_t lease;
    char vendorInfo[PROPERTY_VALUE_MAX];
    char domains[PROPERTY_VALUE_MAX];
    char mtu[PROPERTY_VALUE_MAX];

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    if (nameStr == NULL) return (jboolean)false;

    if (renew) {
        result = ::dhcp_do_request_renew(nameStr, ipaddr, gateway, &prefixLength,
                dns, server, &lease, vendorInfo, domains, mtu);
    } else {
        result = ::dhcp_do_request(nameStr, ipaddr, gateway, &prefixLength,
                dns, server, &lease, vendorInfo, domains, mtu);
    }
    if (result != 0) {
        ALOGD("dhcp_do_request failed : %s (%s)", nameStr, renew ? "renew" : "new");
    }

    env->ReleaseStringUTFChars(ifname, nameStr);
    if (result == 0) {
        env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.clear);

        // set the linkAddress
        // dhcpResults->addLinkAddress(inetAddress, prefixLength)
        result = env->CallBooleanMethod(dhcpResults, dhcpResultsFieldIds.setIpAddress,
                env->NewStringUTF(ipaddr), prefixLength);
    }

    if (result == 0) {
        // set the gateway
        result = env->CallBooleanMethod(dhcpResults,
                dhcpResultsFieldIds.setGateway, env->NewStringUTF(gateway));
    }

    if (result == 0) {
        // dhcpResults->addDns(new InetAddress(dns1))
        result = env->CallBooleanMethod(dhcpResults,
                dhcpResultsFieldIds.addDns, env->NewStringUTF(dns1));
    }

    if (result == 0) {
        env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.setDomains,
                env->NewStringUTF(domains));

        result = env->CallBooleanMethod(dhcpResults,
                dhcpResultsFieldIds.addDns, env->NewStringUTF(dns2));

        if (result == 0) {
            result = env->CallBooleanMethod(dhcpResults,
                    dhcpResultsFieldIds.addDns, env->NewStringUTF(dns3));
            if (result == 0) {
                result = env->CallBooleanMethod(dhcpResults,
                        dhcpResultsFieldIds.addDns, env->NewStringUTF(dns4));
            }
        }
    }

    if (result == 0) {
        // dhcpResults->setServerAddress(new InetAddress(server))
        result = env->CallBooleanMethod(dhcpResults, dhcpResultsFieldIds.setServerAddress,
                env->NewStringUTF(server));
    }

    if (result == 0) {
        // dhcpResults->setLeaseDuration(lease)
        env->CallVoidMethod(dhcpResults,
                dhcpResultsFieldIds.setLeaseDuration, lease);

        // dhcpResults->setVendorInfo(vendorInfo)
        env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.setVendorInfo,
                env->NewStringUTF(vendorInfo));
    }
    return (jboolean)(result == 0);
}


static jboolean android_net_utils_runDhcp(JNIEnv* env, jobject clazz, jstring ifname, jobject info)
{
    return android_net_utils_runDhcpCommon(env, clazz, ifname, info, false);
}

static jboolean android_net_utils_runDhcpRenew(JNIEnv* env, jobject clazz, jstring ifname,
        jobject info)
{
    return android_net_utils_runDhcpCommon(env, clazz, ifname, info, true);
}


static jboolean android_net_utils_stopDhcp(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::dhcp_stop(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}

#if 0  // #ifdef MTK_HARDWARE
/*mtk_net pcscf*/
static jboolean android_net_utils_doSipDhcpRequest(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    ALOGD("android_net_utils_doSipDhcpRequest for %s\n", nameStr);
    result = ::dhcp_do_sip_request(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}

static jboolean android_net_utils_stopSipDhcpRequest(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::dhcp_stop_sip_request(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}

static jboolean android_net_utils_doSipDhcpv6Request(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::dhcpv6_do_sip_request(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}

static jboolean android_net_utils_stopSipDhcpv6Request(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::dhcpv6_stop_sip_request(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}
/*mtk_net pcscf end*/
#endif

static jboolean android_net_utils_releaseDhcpLease(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::dhcp_release_lease(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}

static jstring android_net_utils_getDhcpError(JNIEnv* env, jobject clazz)
{
    return env->NewStringUTF(::dhcp_get_errmsg());
}

static jboolean android_net_utils_bindProcessToNetwork(JNIEnv *env, jobject thiz, jint netId)
{
    return (jboolean) !setNetworkForProcess(netId);
}

static jint android_net_utils_getNetworkBoundToProcess(JNIEnv *env, jobject thiz)
{
    return getNetworkForProcess();
}

static jboolean android_net_utils_bindProcessToNetworkForHostResolution(JNIEnv *env, jobject thiz,
        jint netId)
{
    return (jboolean) !setNetworkForResolv(netId);
}

static jint android_net_utils_bindSocketToNetwork(JNIEnv *env, jobject thiz, jint socket,
        jint netId)
{
    return setNetworkForSocket(netId, socket);
}

static jboolean android_net_utils_protectFromVpn(JNIEnv *env, jobject thiz, jint socket)
{
    return (jboolean) !protectFromVpn(socket);
}

#if 0  // #ifdef MTK_HARDWARE
static jint android_net_utils_runPPPOE(JNIEnv* env, jobject clazz, jstring ifname, jint timeout, jstring usr, jstring passwd, jint interval, jint failure, jint mtu, jint mru, jint mss,jobject dhcpResults)
{
	int result;
	char iplocal[PROPERTY_VALUE_MAX];
	char ipremote[PROPERTY_VALUE_MAX];
    char gateway[PROPERTY_VALUE_MAX];
    char    dns1[PROPERTY_VALUE_MAX];
    char    dns2[PROPERTY_VALUE_MAX];
	char  ppplinkname[PROPERTY_VALUE_MAX];
    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    const char *usrStr = env->GetStringUTFChars(usr, NULL);
    const char *passwdStr = env->GetStringUTFChars(passwd, NULL);
	
	result = ::PPPOE_do_request(nameStr, timeout, usrStr, passwdStr, interval, failure, mtu, mru, mss, iplocal, ipremote, gateway, dns1, dns2, ppplinkname);
	env->ReleaseStringUTFChars(ifname, nameStr);
	env->ReleaseStringUTFChars(usr, usrStr);
	env->ReleaseStringUTFChars(passwd, passwdStr);

	 if (result == 0) {
		 env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.clear);

		 // set the linkAddress
		 // dhcpResults->addLinkAddress(inetAddress, prefixLength)
		 result = env->CallBooleanMethod(dhcpResults, dhcpResultsFieldIds.setIpAddress,
				 env->NewStringUTF(iplocal), 32); // FIXME: we don't have prefixLength here, so use 32 instead, is it good enough?

		 if (result == 0) {
			 // set the gateway
			 // dhcpResults->addGateway(gateway)
			 result = env->CallBooleanMethod(dhcpResults,
					 dhcpResultsFieldIds.setGateway, env->NewStringUTF(gateway));
		 }

		 //env->SetObjectField(info, dhcpInfoInternalFieldIds.dns1, env->NewStringUTF(dns1));
		 if (result == 0) {
			 // dhcpResults->addDns(new InetAddress(dns1))
			 result = env->CallBooleanMethod(dhcpResults,
					 dhcpResultsFieldIds.addDns, env->NewStringUTF(dns1));
		 }

		 //env->SetObjectField(info, dhcpInfoInternalFieldIds.dns2, env->NewStringUTF(dns2));
		 if (result == 0) {
			 result = env->CallBooleanMethod(dhcpResults,
					 dhcpResultsFieldIds.addDns, env->NewStringUTF(dns2));
		 }

		 //env->SetObjectField(info, dhcpInfoInternalFieldIds.interfaceName, env->NewStringUTF(ppplinkname));
		 if (result == 0) {
			 env->CallVoidMethod(dhcpResults,
					 dhcpResultsFieldIds.setPpplinkname, env->NewStringUTF(ppplinkname));
		 }
	 }

	 return result;
}

static jboolean android_net_utils_stopPPPOE(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::PPPOE_stop(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);

    return (jboolean)(result == 0);
}

static jstring android_net_utils_getPPPOEError(JNIEnv* env, jobject clazz)
{
    return env->NewStringUTF(::PPPOE_get_errmsg());    
}

/** M: support reset socket connections by the uid of process */
static jint android_net_utils_resetConnectionByUid(JNIEnv* env, jobject clazz, jint uid)
{
    int etimeout = 110;    /* ETIMEDOUT */
    int result = ::ifc_reset_connection_by_uid(uid, etimeout);  
    ALOGD("android_net_utils_resetConnectionByUid:%d", result);
    return (jint)result;
}
/** @} */

/** M: support reset socket connections by the uid of process with special error number */
static jint android_net_utils_resetConnectionByUid_err(JNIEnv* env, jobject clazz, jint uid, jint err)
{
    int result = ::ifc_reset_connection_by_uid(uid, err);
    ALOGE("android_net_utils_resetConnectionByUid_err:%d", result);
    return (jint)result;
}
/** @} */

/*add dhcpv6 corresponding method as dhcp*/
static jboolean android_net_utils_runDhcpv6Common(JNIEnv* env, jobject clazz, jstring ifname,
        jobject dhcpResults, bool renew)
{
    int result;
    char  ipaddr[PROPERTY_VALUE_MAX];
    char    dns1[PROPERTY_VALUE_MAX];
    char    dns2[PROPERTY_VALUE_MAX];
    uint32_t lease;
    uint32_t pid;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    if (nameStr == NULL) return (jboolean)false;


    if (renew) {
		//pid = env->GetIntField(info, dhcpInfoInternalFieldIds.pid_for_renew);
        result = ::dhcpv6_do_request_renew(nameStr, pid, ipaddr, dns1, dns2, &lease);
    } else {
        result = ::dhcpv6_do_request(nameStr, ipaddr, dns1, dns2, &lease, &pid);
        //env->SetIntField(info, dhcpInfoInternalFieldIds.pid_for_renew, pid);
    }


    env->ReleaseStringUTFChars(ifname, nameStr);

    if (result == 0) {
        env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.clear);

		// set mIfaceName
        // dhcpResults->setInterfaceName(ifname)
		//env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.setInterfaceName, ifname);

		// set the linkAddress
        // dhcpResults->addLinkAddress(inetAddress, prefixLength)
        result = env->CallBooleanMethod(dhcpResults, dhcpResultsFieldIds.setIpAddress,
                env->NewStringUTF(ipaddr), 64);

        result = env->CallBooleanMethod(dhcpResults,
                dhcpResultsFieldIds.addDns, env->NewStringUTF(dns1));

        if (result == 0) {
            result = env->CallBooleanMethod(dhcpResults,
                    dhcpResultsFieldIds.addDns, env->NewStringUTF(dns2));
		}

		if (result == 0) {
			// dhcpResults->setLeaseDuration(lease)
			env->CallVoidMethod(dhcpResults,
					dhcpResultsFieldIds.setLeaseDuration, lease);
		}

		if (result == 0)
		{
			env->CallVoidMethod(dhcpResults,
					dhcpResultsFieldIds.setPidForRenew, pid);
		}
    }
    
    return (jboolean)(result == 0);
}

static jboolean android_net_utils_runDhcpv6(JNIEnv* env, jobject clazz, jstring ifname, jobject info)
{
    return android_net_utils_runDhcpv6Common(env, clazz, ifname, info, false);
}

static jboolean android_net_utils_runDhcpv6Renew(JNIEnv* env, jobject clazz, jstring ifname, jobject info)
{
    return android_net_utils_runDhcpv6Common(env, clazz, ifname, info, true);
}

static jboolean android_net_utils_stopDhcpv6(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::dhcpv6_stop(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}

static jstring android_net_utils_getDhcpv6Error(JNIEnv* env, jobject clazz)
{
    return env->NewStringUTF(::dhcpv6_get_errmsg());
}

static jint android_net_utils_getRaFlags(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result, len, fd;
    char filename[64];
	char flags;
    const char *nameStr = env->GetStringUTFChars(ifname, NULL);

	snprintf(filename, sizeof(filename), "/proc/sys/net/ipv6/conf/%s/ra_info_flag", nameStr);
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		ALOGE("Can't open %s: %s", filename, strerror(errno));
		result = -1;
	} else {
		len = read(fd, &flags, 1);
		if (len < 0) {
			ALOGE("Can't read %s: %s", filename, strerror(errno));
			result = -2;
		} else {
			if(flags >= '0' && flags <= '4'){
				result = (int)(flags - '0');
				ALOGD("read:ra_info_flag=%c, result=%d\n", flags, result);
			} else {
				ALOGE("read:ra_info_flag=0x%x\n", flags);
				result = -3;
			}
		}	
		close(fd);
	}	

    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jint)result;
}

/*JNI of PD*/
static jboolean android_net_utils_runPDCommon(JNIEnv* env, jobject clazz, jstring ifname, jobject dhcpResults, bool renew)
{
	int result;
    char  prefix[PROPERTY_VALUE_MAX];
    uint32_t lease;

	const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    if (nameStr == NULL) return (jboolean)false;

	if (renew) {
		result = ::dhcpv6_PD_renew(nameStr, prefix, &lease);
	} else {
		result = ::dhcpv6_PD_request(nameStr, prefix, &lease);
	}

    env->ReleaseStringUTFChars(ifname, nameStr);

    if (result == 0) {
        env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.clear);

		// set mIfaceName
        // dhcpResults->setInterfaceName(ifname)
		//env->CallVoidMethod(dhcpResults, dhcpResultsFieldIds.setInterfaceName, ifname);

		// set the linkAddress
        //env->SetObjectField(info, dhcpInfoInternalFieldIds.ipaddress, env->NewStringUTF(prefix));
        result = env->CallBooleanMethod(dhcpResults, dhcpResultsFieldIds.setIpAddress,
                env->NewStringUTF(prefix), 64);

        //env->SetIntField(info, dhcpInfoInternalFieldIds.leaseDuration, lease);
		if (result == 0) {
			// dhcpResults->setLeaseDuration(lease)
			env->CallVoidMethod(dhcpResults,
					dhcpResultsFieldIds.setLeaseDuration, lease);
		}
    }
    
    return (jboolean)(result == 0);
}

static jboolean android_net_utils_runPD(JNIEnv* env, jobject clazz, jstring ifname, jobject info)
{
    return android_net_utils_runPDCommon(env, clazz, ifname, info, false);
}

static jboolean android_net_utils_runPDRenew(JNIEnv* env, jobject clazz, jstring ifname, jobject info)
{
    return android_net_utils_runPDCommon(env, clazz, ifname, info, true);
}

static jboolean android_net_utils_stopPD(JNIEnv* env, jobject clazz, jstring ifname)
{
    int result;

    const char *nameStr = env->GetStringUTFChars(ifname, NULL);
    result = ::dhcpv6_PD_stop(nameStr);
    env->ReleaseStringUTFChars(ifname, nameStr);
    return (jboolean)(result == 0);
}

static jstring android_net_utils_getPDError(JNIEnv* env, jobject clazz)
{
    return env->NewStringUTF(::PD_get_errmsg());
}
#endif
// ----------------------------------------------------------------------------

/*
 * JNI registration.
 */
static JNINativeMethod gNetworkUtilMethods[] = {
    /* name, signature, funcPtr */
    { "resetConnections", "(Ljava/lang/String;I)I",  (void *)android_net_utils_resetConnections },
    { "runDhcp", "(Ljava/lang/String;Landroid/net/DhcpResults;)Z",  (void *)android_net_utils_runDhcp },
    { "runDhcpRenew", "(Ljava/lang/String;Landroid/net/DhcpResults;)Z",  (void *)android_net_utils_runDhcpRenew },
    { "stopDhcp", "(Ljava/lang/String;)Z",  (void *)android_net_utils_stopDhcp },
    { "releaseDhcpLease", "(Ljava/lang/String;)Z",  (void *)android_net_utils_releaseDhcpLease },
    { "getDhcpError", "()Ljava/lang/String;", (void*) android_net_utils_getDhcpError },
    { "bindProcessToNetwork", "(I)Z", (void*) android_net_utils_bindProcessToNetwork },
    { "getNetworkBoundToProcess", "()I", (void*) android_net_utils_getNetworkBoundToProcess },
    { "bindProcessToNetworkForHostResolution", "(I)Z", (void*) android_net_utils_bindProcessToNetworkForHostResolution },
    { "bindSocketToNetwork", "(II)I", (void*) android_net_utils_bindSocketToNetwork },
    { "protectFromVpn", "(I)Z", (void*)android_net_utils_protectFromVpn },
#if 0  // #ifdef MTK_HARDWARE
	{"runPPPOE", "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;IIIIILandroid/net/DhcpResults;)I", (void*)android_net_utils_runPPPOE },
	{"stopPPPOE", "(Ljava/lang/String;)Z", (void*)android_net_utils_stopPPPOE },
    { "getPPPOEError", "()Ljava/lang/String;", (void*) android_net_utils_getPPPOEError },
    /// M: MTK sip info utility. Must sync with netutils.java !!
    { "doSipDhcpRequest", "(Ljava/lang/String;)Z",  (void *)android_net_utils_doSipDhcpRequest },
    { "stopSipDhcpRequest", "(Ljava/lang/String;)Z",  (void *)android_net_utils_stopSipDhcpRequest },
	{ "doSipDhcpv6Request", "(Ljava/lang/String;)Z",	(void *)android_net_utils_doSipDhcpv6Request },
	{ "stopSipDhcpv6Request", "(Ljava/lang/String;)Z",  (void *)android_net_utils_stopSipDhcpv6Request },
    /// M: MTK network utility functions
    { "resetConnectionByUidErrNum", "(II)I",  (void *)android_net_utils_resetConnectionByUid_err },
    { "resetConnectionByUid", "(I)I",  (void *)android_net_utils_resetConnectionByUid },
    /// add dhcpv6 corresponding JNI declaration
    { "runDhcpv6", "(Ljava/lang/String;Landroid/net/DhcpResults;)Z",  (void *)android_net_utils_runDhcpv6 },
    { "runDhcpv6Renew", "(Ljava/lang/String;Landroid/net/DhcpResults;)Z",  (void *)android_net_utils_runDhcpv6Renew },
    { "stopDhcpv6", "(Ljava/lang/String;)Z",  (void *)android_net_utils_stopDhcpv6 },
    { "getDhcpv6Error", "()Ljava/lang/String;", (void*) android_net_utils_getDhcpv6Error },
	{ "getRaFlags", "(Ljava/lang/String;)I",  (void *)android_net_utils_getRaFlags },
	/// M: add PD corresponding JNI declaration
    { "runDhcpv6PD", "(Ljava/lang/String;Landroid/net/DhcpResults;)Z",  (void *)android_net_utils_runPD },
    { "runDhcpv6PDRenew", "(Ljava/lang/String;Landroid/net/DhcpResults;)Z",  (void *)android_net_utils_runPDRenew },
    { "stopDhcpv6PD", "(Ljava/lang/String;)Z",  (void *)android_net_utils_stopPD },
    { "getDhcpv6PDError", "()Ljava/lang/String;", (void*)android_net_utils_getPDError },
#endif
};

int register_android_net_NetworkUtils(JNIEnv* env)
{
    jclass dhcpResultsClass = env->FindClass("android/net/DhcpResults");
    LOG_FATAL_IF(dhcpResultsClass == NULL, "Unable to find class android/net/DhcpResults");
    dhcpResultsFieldIds.clear =
            env->GetMethodID(dhcpResultsClass, "clear", "()V");
    dhcpResultsFieldIds.setIpAddress =
            env->GetMethodID(dhcpResultsClass, "setIpAddress", "(Ljava/lang/String;I)Z");
    dhcpResultsFieldIds.setGateway =
            env->GetMethodID(dhcpResultsClass, "setGateway", "(Ljava/lang/String;)Z");
    dhcpResultsFieldIds.addDns =
            env->GetMethodID(dhcpResultsClass, "addDns", "(Ljava/lang/String;)Z");
    dhcpResultsFieldIds.setDomains =
            env->GetMethodID(dhcpResultsClass, "setDomains", "(Ljava/lang/String;)V");
    dhcpResultsFieldIds.setServerAddress =
            env->GetMethodID(dhcpResultsClass, "setServerAddress", "(Ljava/lang/String;)Z");
    dhcpResultsFieldIds.setLeaseDuration =
            env->GetMethodID(dhcpResultsClass, "setLeaseDuration", "(I)V");
    dhcpResultsFieldIds.setVendorInfo =
            env->GetMethodID(dhcpResultsClass, "setVendorInfo", "(Ljava/lang/String;)V");
#if 0  // #ifdef MTK_HARDWARE
    dhcpResultsFieldIds.setPidForRenew =
			env->GetMethodID(dhcpResultsClass, "setPidForRenew", "(I)V");
    dhcpResultsFieldIds.setPpplinkname = 
			env->GetMethodID(dhcpResultsClass, "setPpplinkname", "(Ljava/lang/String;)V");
#endif

    return AndroidRuntime::registerNativeMethods(env,
            NETUTILS_PKG_NAME, gNetworkUtilMethods, NELEM(gNetworkUtilMethods));
}

}; // namespace android
