/*
Copyright (c) 2013 - 2017, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
		* Redistributions of source code must retain the above copyright
			notice, this list of conditions and the following disclaimer.
		* Redistributions in binary form must reproduce the above
			copyright notice, this list of conditions and the following
			disclaimer in the documentation and/or other materials provided
			with the distribution.
		* Neither the name of The Linux Foundation nor the names of its
			contributors may be used to endorse or promote products derived
			from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "ipa_nat_drv.h"
#include "ipa_nat_drvi.h"

#ifdef USE_GLIB
#include <glib.h>
#define strlcpy g_strlcpy
#else
#ifndef FEATURE_IPA_ANDROID
static size_t strlcpy(char * dst, const char * src, size_t size) {
	if (size < 1)
		return 0;
	strncpy(dst, src, size - 1);
	dst[size - 1] = 0;
	return strlen(dst);
}
#endif
#endif


struct ipa_nat_cache ipv4_nat_cache;
pthread_mutex_t nat_mutex    = PTHREAD_MUTEX_INITIALIZER;

static ipa_nat_pdn_entry pdns[IPA_MAX_PDN_NUM];

/* ------------------------------------------
		UTILITY FUNCTIONS START
	 --------------------------------------------*/

/**
 * UpdateSwSpecParams() - updates sw specific params
 * @rule: [in/out] nat table rule
 * @param_type: [in] which param need to update
 * @value: [in] value of param
 *
 * Update SW specific params in the passed rule.
 *
 * Returns: None
 */
void UpdateSwSpecParams(struct ipa_nat_rule *rule,
															uint8_t param_type,
															uint32_t value)
{
	uint32_t temp = rule->sw_spec_params;

	if (IPA_NAT_SW_PARAM_INDX_TBL_ENTRY_BYTE == param_type) {
		value = (value << INDX_TBL_ENTRY_SIZE_IN_BITS);
		temp &= 0x0000FFFF;
	} else {
		temp &= 0xFFFF0000;
	}

	temp = (temp | value);
	rule->sw_spec_params = temp;
	return;
}

/**
 * Read8BitFieldValue()
 * @rule: [in/out]
 * @param_type: [in]
 * @value: [in]
 *
 *
 *
 * Returns: None
 */

uint8_t Read8BitFieldValue(uint32_t param,
														ipa_nat_rule_field_type fld_type)
{
	void *temp = (void *)&param;

	switch (fld_type) {

	case PROTOCOL_FIELD:
		return ((time_stamp_proto *)temp)->protocol;

	default:
		IPAERR("Invalid Field type passed\n");
		return 0;
	}
}

uint16_t Read16BitFieldValue(uint32_t param,
														 ipa_nat_rule_field_type fld_type)
{
	void *temp = (void *)&param;

	switch (fld_type) {

	case NEXT_INDEX_FIELD:
		return ((next_index_pub_port *)temp)->next_index;

	case PUBLIC_PORT_FILED:
		return ((next_index_pub_port *)temp)->public_port;

	case ENABLE_FIELD:
		return ((ipcksum_enbl *)temp)->enable;

	case SW_SPEC_PARAM_PREV_INDEX_FIELD:
		return ((sw_spec_params *)temp)->prev_index;

	case SW_SPEC_PARAM_INDX_TBL_ENTRY_FIELD:
		return ((sw_spec_params *)temp)->index_table_entry;

	case INDX_TBL_TBL_ENTRY_FIELD:
		return ((tbl_ent_nxt_indx *)temp)->tbl_entry;

	case INDX_TBL_NEXT_INDEX_FILED:
		return ((tbl_ent_nxt_indx *)temp)->next_index;

#ifdef NAT_DUMP
	case IP_CHKSUM_FIELD:
		return ((ipcksum_enbl *)temp)->ip_chksum;
#endif

	default:
		IPAERR("Invalid Field type passed\n");
		return 0;
	}
}

uint32_t Read32BitFieldValue(uint32_t param,
														 ipa_nat_rule_field_type fld_type)
{

	void *temp = (void *)&param;

	switch (fld_type) {

	case TIME_STAMP_FIELD:
		return ((time_stamp_proto *)temp)->time_stamp;

	default:
		IPAERR("Invalid Field type passed\n");
		return 0;
	}
}

/**
* GetIPAVer(void) - store IPA HW ver in cache
*
*
* Returns: 0 on success, negative on failure
*/
int GetIPAVer(void)
{
	int ret;

	ret = ioctl(ipv4_nat_cache.ipa_fd, IPA_IOC_GET_HW_VERSION, &ipv4_nat_cache.ver);
	if (ret != 0) {
		perror("GetIPAVer(): ioctl error value");
		IPAERR("unable to get IPA version. Error ;%d\n", ret);
		IPADBG("ipa fd %d\n", ipv4_nat_cache.ipa_fd);
		return -EINVAL;
	}
	IPADBG("IPA version is %d\n", ipv4_nat_cache.ver);
	return 0;
}

/**
 * CreateNatDevice() - Create nat devices
 * @mem: [in] name of device that need to create
 *
 * Create Nat device and Register for file create
 * notification in given directory and wait till
 * receive notification
 *
 * Returns: 0 on success, negative on failure
 */
int CreateNatDevice(struct ipa_ioc_nat_alloc_mem *mem)
{
	int ret;

	ret = ioctl(ipv4_nat_cache.ipa_fd, IPA_IOC_ALLOC_NAT_MEM, mem);
	if (ret != 0) {
		perror("CreateNatDevice(): ioctl error value");
		IPAERR("unable to post nat mem init. Error ;%d\n", ret);
		IPADBG("ipa fd %d\n", ipv4_nat_cache.ipa_fd);
		return -EINVAL;
	}
	IPADBG("posted IPA_IOC_ALLOC_NAT_MEM to kernel successfully\n");
	return 0;
}

/**
 * GetNearest2Power() - Returns the nearest power of 2
 * @num: [in] given number
 * @ret: [out] nearest power of 2
 *
 * Returns the nearest power of 2 for a
 * given number
 *
 * Returns: 0 on success, negative on failure
 */
int GetNearest2Power(uint16_t num, uint16_t *ret)
{
	uint16_t number = num;
	uint16_t tmp = 1;
	*ret = 0;

	if (0 == num) {
		return -EINVAL;
	}

	if (1 == num) {
		*ret = 2;
		return 0;
	}

	for (;;) {
		if (1 == num) {
			if (number != tmp) {
				tmp *= 2;
			}

			*ret = tmp;
			return 0;
		}

		num >>= 1;
		tmp *= 2;
	}

	return -EINVAL;
}

/**
 * GetNearestEven() - Returns the nearest even number
 * @num: [in] given number
 * @ret: [out] nearest even number
 *
 * Returns the nearest even number for a given number
 *
 * Returns: 0 on success, negative on failure
 */
void GetNearestEven(uint16_t num, uint16_t *ret)
{

	if (num < 2) {
		*ret = 2;
		return;
	}

	while ((num % 2) != 0) {
		num = num + 1;
	}

	*ret = num;
	return;
}

/**
 * dst_hash() - Find the index into ipv4 base table
 * @public_ip: [in] public_ip
 * @trgt_ip: [in] Target IP address
 * @trgt_port: [in]  Target port
 * @public_port: [in]  Public port
 * @proto: [in] Protocol (TCP/IP)
 * @size: [in] size of the ipv4 base Table
 *
 * This hash method is used to find the hash index of new nat
 * entry into ipv4 base table. In case of zero index, the
 * new entry will be stored into N-1 index where N is size of
 * ipv4 base table
 *
 * Returns: >0 index into ipv4 base table, negative on failure
 */
static uint16_t dst_hash(uint32_t public_ip, uint32_t trgt_ip,
			uint16_t trgt_port, uint16_t public_port,
			uint8_t proto, uint16_t size)
{
	uint16_t hash = ((uint16_t)(trgt_ip)) ^ ((uint16_t)(trgt_ip >> 16)) ^
		 (trgt_port) ^ (public_port) ^ (proto);

	if (ipv4_nat_cache.ver >= IPA_HW_v4_0)
		hash ^= ((uint16_t)(public_ip)) ^
		((uint16_t)(public_ip >> 16));

	IPADBG("public ip 0x%X\n", public_ip);
	IPADBG("trgt_ip: 0x%x trgt_port: 0x%x\n", trgt_ip, trgt_port);
	IPADBG("public_port: 0x%x\n", public_port);
	IPADBG("proto: 0x%x size: 0x%x\n", proto, size);

	hash = (hash & size);

	/* If the hash resulted to zero then set it to maximum value
		 as zero is unused entry in nat tables */
	if (0 == hash) {
		return size;
	}

	IPADBG("dst_hash returning value: %d\n", hash);
	return hash;
}

/**
 * src_hash() - Find the index into ipv4 index base table
 * @priv_ip: [in] Private IP address
 * @priv_port: [in]  Private port
 * @trgt_ip: [in]  Target IP address
 * @trgt_port: [in] Target Port
 * @proto: [in]  Protocol (TCP/IP)
 * @size: [in] size of the ipv4 index base Table
 *
 * This hash method is used to find the hash index of new nat
 * entry into ipv4 index base table. In case of zero index, the
 * new entry will be stored into N-1 index where N is size of
 * ipv4 index base table
 *
 * Returns: >0 index into ipv4 index base table, negative on failure
 */
static uint16_t src_hash(uint32_t priv_ip, uint16_t priv_port,
				uint32_t trgt_ip, uint16_t trgt_port,
				uint8_t proto, uint16_t size)
{
	uint16_t hash =  ((uint16_t)(priv_ip)) ^ ((uint16_t)(priv_ip >> 16)) ^
		 (priv_port) ^
		 ((uint16_t)(trgt_ip)) ^ ((uint16_t)(trgt_ip >> 16)) ^
		 (trgt_port) ^ (proto);

	IPADBG("priv_ip: 0x%x priv_port: 0x%x\n", priv_ip, priv_port);
	IPADBG("trgt_ip: 0x%x trgt_port: 0x%x\n", trgt_ip, trgt_port);
	IPADBG("proto: 0x%x size: 0x%x\n", proto, size);

	hash = (hash & size);

	/* If the hash resulted to zero then set it to maximum value
		 as zero is unused entry in nat tables */
	if (0 == hash) {
		return size;
	}

	IPADBG("src_hash returning value: %d\n", hash);
	return hash;
}

/**
 * ipa_nati_calc_ip_cksum() - Calculate the source nat
 *														 IP checksum diff
 * @pub_ip_addr: [in] public ip address
 * @priv_ip_addr: [in]	Private ip address
 *
 * source nat ip checksum different is calculated as
 * public_ip_addr - private_ip_addr
 * Here we are using 1's complement to represent -ve number.
 * So take 1's complement of private ip addr and add it
 * to public ip addr.
 *
 * Returns: >0 ip checksum diff
 */
static uint16_t ipa_nati_calc_ip_cksum(uint32_t pub_ip_addr,
										uint32_t priv_ip_addr)
{
	uint16_t ret;
	uint32_t cksum = 0;

	/* Add LSB(2 bytes) of public ip address to cksum */
	cksum += (pub_ip_addr & 0xFFFF);

	/* Add MSB(2 bytes) of public ip address to cksum
		and check for carry forward(CF), if any add it
	*/
	cksum += (pub_ip_addr>>16);
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* Calculate the 1's complement of private ip address */
	priv_ip_addr = (~priv_ip_addr);

	/* Add LSB(2 bytes) of private ip address to cksum
		 and check for carry forward(CF), if any add it
	*/
	cksum += (priv_ip_addr & 0xFFFF);
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* Add MSB(2 bytes) of private ip address to cksum
		 and check for carry forward(CF), if any add it
	*/
	cksum += (priv_ip_addr>>16);
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* Return the LSB(2 bytes) of checksum	*/
	ret = (uint16_t)cksum;
	return ret;
}

/**
 * ipa_nati_calc_tcp_udp_cksum() - Calculate the source nat
 *																TCP/UDP checksum diff
 * @pub_ip_addr: [in] public ip address
 * @pub_port: [in] public tcp/udp port
 * @priv_ip_addr: [in]	Private ip address
 * @priv_port: [in] Private tcp/udp prot
 *
 * source nat tcp/udp checksum is calculated as
 * (pub_ip_addr + pub_port) - (priv_ip_addr + priv_port)
 * Here we are using 1's complement to represent -ve number.
 * So take 1's complement of prviate ip addr &private port
 * and add it public ip addr & public port.
 *
 * Returns: >0 tcp/udp checksum diff
 */
static uint16_t ipa_nati_calc_tcp_udp_cksum(uint32_t pub_ip_addr,
										uint16_t pub_port,
										uint32_t priv_ip_addr,
										uint16_t priv_port)
{
	uint16_t ret = 0;
	uint32_t cksum = 0;

	/* Add LSB(2 bytes) of public ip address to cksum */
	cksum += (pub_ip_addr & 0xFFFF);

	/* Add MSB(2 bytes) of public ip address to cksum
		and check for carry forward(CF), if any add it
	*/
	cksum += (pub_ip_addr>>16);
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* Add public port to cksum and
		 check for carry forward(CF), if any add it */
	cksum += pub_port;
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* Calculate the 1's complement of private ip address */
	priv_ip_addr = (~priv_ip_addr);

	/* Add LSB(2 bytes) of private ip address to cksum
		 and check for carry forward(CF), if any add it
	*/
	cksum += (priv_ip_addr & 0xFFFF);
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* Add MSB(2 bytes) of private ip address to cksum
		 and check for carry forward(CF), if any add
	*/
	cksum += (priv_ip_addr>>16);
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* Calculate the 1's complement of private port */
	priv_port = (~priv_port);

	/* Add public port to cksum and
	 check for carry forward(CF), if any add it */
	cksum += priv_port;
	if (cksum >> 16) {
		cksum = (cksum & 0x0000FFFF);
		cksum += 1;
	}

	/* return the LSB(2 bytes) of checksum */
	ret = (uint16_t)cksum;
	return ret;
}

/**
 * ipa_nati_make_rule_hdl() - makes nat rule handle
 * @tbl_hdl: [in] nat table handle
 * @tbl_entry: [in]  nat table entry
 *
 * Calculate the nat rule handle which from
 * nat entry which will be returned to client of
 * nat driver
 *
 * Returns: >0 nat rule handle
 */
uint16_t ipa_nati_make_rule_hdl(uint16_t tbl_hdl,
				uint16_t tbl_entry)
{
	struct ipa_nat_ip4_table_cache *tbl_ptr;
	uint16_t rule_hdl = 0;
	uint16_t cnt = 0;

	tbl_ptr = &ipv4_nat_cache.ip4_tbl[tbl_hdl-1];

	if (tbl_entry >= tbl_ptr->table_entries) {
		/* Increase the current expansion table count */
		tbl_ptr->cur_expn_tbl_cnt++;

		/* Update the index into table */
		rule_hdl = tbl_entry - tbl_ptr->table_entries;
		rule_hdl = (rule_hdl << IPA_NAT_RULE_HDL_TBL_TYPE_BITS);
		/* Update the table type mask */
		rule_hdl = (rule_hdl | IPA_NAT_RULE_HDL_TBL_TYPE_MASK);
	} else {
		/* Increase the current count */
		tbl_ptr->cur_tbl_cnt++;

		rule_hdl = tbl_entry;
		rule_hdl = (rule_hdl << IPA_NAT_RULE_HDL_TBL_TYPE_BITS);
	}

	for (; cnt < (tbl_ptr->table_entries + tbl_ptr->expn_table_entries); cnt++) {
		if (IPA_NAT_INVALID_NAT_ENTRY == tbl_ptr->rule_id_array[cnt]) {
			tbl_ptr->rule_id_array[cnt] = rule_hdl;
			return cnt + 1;
		}
	}

	return 0;
}

/**
 * ipa_nati_parse_ipv4_rule_hdl() - prase rule handle
 * @tbl_hdl:	[in] nat table rule
 * @rule_hdl: [in] nat rule handle
 * @expn_tbl: [out] expansion table or not
 * @tbl_entry: [out] index into table
 *
 * Parse the rule handle to retrieve the nat table
 * type and entry of nat table
 *
 * Returns: None
 */
void ipa_nati_parse_ipv4_rule_hdl(uint8_t tbl_index,
				uint16_t rule_hdl, uint8_t *expn_tbl,
				uint16_t *tbl_entry)
{
	struct ipa_nat_ip4_table_cache *tbl_ptr;
	uint16_t rule_id;

	*expn_tbl = 0;
	*tbl_entry = IPA_NAT_INVALID_NAT_ENTRY;
	tbl_ptr = &ipv4_nat_cache.ip4_tbl[tbl_index];

	if (rule_hdl >= (tbl_ptr->table_entries + tbl_ptr->expn_table_entries)) {
		IPAERR("invalid rule handle\n");
		return;
	}

	rule_id = tbl_ptr->rule_id_array[rule_hdl-1];

	/* Retrieve the table type */
	*expn_tbl = 0;
	if (rule_id & IPA_NAT_RULE_HDL_TBL_TYPE_MASK) {
		*expn_tbl = 1;
	}

	/* Retrieve the table entry */
	*tbl_entry = (rule_id >> IPA_NAT_RULE_HDL_TBL_TYPE_BITS);
	return;
}

uint32_t ipa_nati_get_entry_offset(struct ipa_nat_ip4_table_cache *cache_ptr,
						nat_table_type tbl_type,
						uint16_t	tbl_entry)
{
	struct ipa_nat_rule *tbl_ptr;
	uint32_t ret = 0;

	if (IPA_NAT_EXPN_TBL == tbl_type) {
		tbl_ptr = (struct ipa_nat_rule *)cache_ptr->ipv4_expn_rules_addr;
	} else {
		tbl_ptr = (struct ipa_nat_rule *)cache_ptr->ipv4_rules_addr;
	}

	ret = (char *)&tbl_ptr[tbl_entry] - (char *)tbl_ptr;
	ret += cache_ptr->tbl_addr_offset;
	return ret;
}

uint32_t ipa_nati_get_index_entry_offset(struct ipa_nat_ip4_table_cache *cache_ptr,
								nat_table_type tbl_type,
								uint16_t indx_tbl_entry)
{
	struct ipa_nat_indx_tbl_rule *indx_tbl_ptr;
	uint32_t ret = 0;

	if (IPA_NAT_INDEX_EXPN_TBL == tbl_type) {
		indx_tbl_ptr =
			 (struct ipa_nat_indx_tbl_rule *)cache_ptr->index_table_expn_addr;
	} else {
		indx_tbl_ptr =
			 (struct ipa_nat_indx_tbl_rule *)cache_ptr->index_table_addr;
	}

	ret = (char *)&indx_tbl_ptr[indx_tbl_entry] - (char *)indx_tbl_ptr;
	ret += cache_ptr->tbl_addr_offset;
	return ret;
}

/* ------------------------------------------
		UTILITY FUNCTIONS END
--------------------------------------------*/

/* ------------------------------------------
	 Main Functions
--------------------------------------------**/
void ipa_nati_reset_tbl(uint8_t tbl_indx)
{
	uint16_t table_entries = ipv4_nat_cache.ip4_tbl[tbl_indx].table_entries;
	uint16_t expn_table_entries = ipv4_nat_cache.ip4_tbl[tbl_indx].expn_table_entries;

	/* Base table */
	IPADBG("memset() base table to 0, %p\n",
				 ipv4_nat_cache.ip4_tbl[tbl_indx].ipv4_rules_addr);

	memset(ipv4_nat_cache.ip4_tbl[tbl_indx].ipv4_rules_addr,
				 0,
				 IPA_NAT_TABLE_ENTRY_SIZE * table_entries);

	/* Base expansino table */
	IPADBG("memset() expn base table to 0, %p\n",
				 ipv4_nat_cache.ip4_tbl[tbl_indx].ipv4_expn_rules_addr);

	memset(ipv4_nat_cache.ip4_tbl[tbl_indx].ipv4_expn_rules_addr,
				 0,
				 IPA_NAT_TABLE_ENTRY_SIZE * expn_table_entries);

	/* Index table */
	IPADBG("memset() index table to 0, %p\n",
				 ipv4_nat_cache.ip4_tbl[tbl_indx].index_table_addr);

	memset(ipv4_nat_cache.ip4_tbl[tbl_indx].index_table_addr,
				 0,
				 IPA_NAT_INDEX_TABLE_ENTRY_SIZE * table_entries);

	/* Index expansion table */
	IPADBG("memset() index expn table to 0, %p\n",
				 ipv4_nat_cache.ip4_tbl[tbl_indx].index_table_expn_addr);

	memset(ipv4_nat_cache.ip4_tbl[tbl_indx].index_table_expn_addr,
				 0,
				 IPA_NAT_INDEX_TABLE_ENTRY_SIZE * expn_table_entries);

	IPADBG("returning from ipa_nati_reset_tbl()\n");
	return;
}

int ipa_nati_add_ipv4_tbl(uint32_t public_ip_addr,
				uint16_t number_of_entries,
				uint32_t *tbl_hdl)
{
	struct ipa_ioc_nat_alloc_mem mem;
	uint8_t tbl_indx = ipv4_nat_cache.table_cnt;
	uint16_t table_entries, expn_table_entries;
	int ret;

	*tbl_hdl = 0;
	/* Allocate table */
	memset(&mem, 0, sizeof(mem));
	ret = ipa_nati_alloc_table(number_of_entries,
														 &mem,
														 &table_entries,
														 &expn_table_entries);
	if (0 != ret) {
		IPAERR("unable to allocate nat table\n");
		return -ENOMEM;
	}

	/* Update the cache
		 The (IPA_NAT_UNUSED_BASE_ENTRIES/2) indicates zero entry entries
		 for both base and expansion table
	*/
	ret = ipa_nati_update_cache(&mem,
															public_ip_addr,
															table_entries,
															expn_table_entries);
	if (0 != ret) {
		IPAERR("unable to update cache Error: %d\n", ret);
		return -EINVAL;
	}

	/* Reset the nat table before posting init cmd */
	ipa_nati_reset_tbl(tbl_indx);

	/* Initialize the ipa hw with nat table dimensions */
	ret = ipa_nati_post_ipv4_init_cmd(tbl_indx);
	if (0 != ret) {
		IPAERR("unable to post nat_init command Error %d\n", ret);
		return -EINVAL;
	}

	/* store the initial public ip address in the cached pdn table
		this is backward compatible for pre IPAv4 versions, we will always
		use this ip as the single PDN address
	*/
	pdns[0].public_ip = public_ip_addr;

	/* Return table handle */
	ipv4_nat_cache.table_cnt++;
	*tbl_hdl = ipv4_nat_cache.table_cnt;

#ifdef NAT_DUMP
	ipa_nat_dump_ipv4_table(*tbl_hdl);
#endif
	return 0;
}

int ipa_nati_alloc_table(uint16_t number_of_entries,
				struct ipa_ioc_nat_alloc_mem *mem,
				uint16_t *table_entries,
				uint16_t *expn_table_entries)
{
	int fd = 0, ret;
	uint16_t total_entries;

	/* Copy the table name */
	strlcpy(mem->dev_name, NAT_DEV_NAME, IPA_RESOURCE_NAME_MAX);

	/* Calculate the size for base table and expansion table */
	*table_entries = (uint16_t)(number_of_entries * IPA_NAT_BASE_TABLE_PERCENTAGE);
	if (*table_entries == 0) {
		*table_entries = 1;
	}
	if (GetNearest2Power(*table_entries, table_entries)) {
		IPAERR("unable to calculate power of 2\n");
		return -EINVAL;
	}

	*expn_table_entries = (uint16_t)(number_of_entries * IPA_NAT_EXPANSION_TABLE_PERCENTAGE);
	GetNearestEven(*expn_table_entries, expn_table_entries);

	total_entries = (*table_entries)+(*expn_table_entries);

	/* Calclate the memory size for both table and index table entries */
	mem->size = (IPA_NAT_TABLE_ENTRY_SIZE * total_entries);
	IPADBG("Nat Table size: %zu\n", mem->size);
	mem->size += (IPA_NAT_INDEX_TABLE_ENTRY_SIZE * total_entries);
	IPADBG("Nat Base and Index Table size: %zu\n", mem->size);

	if (!ipv4_nat_cache.ipa_fd) {
		fd = open(IPA_DEV_NAME, O_RDONLY);
		if (fd < 0) {
			perror("ipa_nati_alloc_table(): open error value:");
			IPAERR("unable to open ipa device\n");
			return -EIO;
		}
		ipv4_nat_cache.ipa_fd = fd;
	}

	if (GetIPAVer()) {
		IPAERR("unable to get ipa ver\n");
		return -EIO;
	}

	ret = CreateNatDevice(mem);
	return ret;
}


int ipa_nati_update_cache(struct ipa_ioc_nat_alloc_mem *mem,
				uint32_t public_addr,
				uint16_t tbl_entries,
				uint16_t expn_tbl_entries)
{
	uint32_t index = ipv4_nat_cache.table_cnt;
	char *ipv4_rules_addr = NULL;

	int fd = 0;
	int flags = MAP_SHARED;
	int prot = PROT_READ | PROT_WRITE;
	off_t offset = 0;
#ifdef IPA_ON_R3PC
	int ret = 0;
	uint32_t nat_mem_offset = 0;
#endif

	ipv4_nat_cache.ip4_tbl[index].valid = IPA_NAT_TABLE_VALID;
	ipv4_nat_cache.ip4_tbl[index].public_addr = public_addr;
	ipv4_nat_cache.ip4_tbl[index].size = mem->size;
	ipv4_nat_cache.ip4_tbl[index].tbl_addr_offset = mem->offset;

	ipv4_nat_cache.ip4_tbl[index].table_entries = tbl_entries;
	ipv4_nat_cache.ip4_tbl[index].expn_table_entries = expn_tbl_entries;

	IPADBG("num of ipv4 rules:%d\n", tbl_entries);
	IPADBG("num of ipv4 expn rules:%d\n", expn_tbl_entries);

	/* allocate memory for nat index expansion table */
	if (NULL == ipv4_nat_cache.ip4_tbl[index].index_expn_table_meta) {
		ipv4_nat_cache.ip4_tbl[index].index_expn_table_meta =
			 malloc(sizeof(struct ipa_nat_indx_tbl_meta_info) * expn_tbl_entries);

		if (NULL == ipv4_nat_cache.ip4_tbl[index].index_expn_table_meta) {
			IPAERR("Fail to allocate ipv4 index expansion table meta\n");
			return 0;
		}

		memset(ipv4_nat_cache.ip4_tbl[index].index_expn_table_meta,
					 0,
					 sizeof(struct ipa_nat_indx_tbl_meta_info) * expn_tbl_entries);
	}

	/* Allocate memory for rule_id_array */
	if (NULL == ipv4_nat_cache.ip4_tbl[index].rule_id_array) {
		ipv4_nat_cache.ip4_tbl[index].rule_id_array =
			 malloc(sizeof(uint16_t) * (tbl_entries + expn_tbl_entries));

		if (NULL == ipv4_nat_cache.ip4_tbl[index].rule_id_array) {
			IPAERR("Fail to allocate rule id array\n");
			return 0;
		}

		memset(ipv4_nat_cache.ip4_tbl[index].rule_id_array,
					 0,
					 sizeof(uint16_t) * (tbl_entries + expn_tbl_entries));
	}


	/* open the nat table */
	strlcpy(mem->dev_name, NAT_DEV_FULL_NAME, IPA_RESOURCE_NAME_MAX);
	fd = open(mem->dev_name, O_RDWR);
	if (fd < 0) {
		perror("ipa_nati_update_cache(): open error value:");
		IPAERR("unable to open nat device. Error:%d\n", fd);
		return -EIO;
	}

	/* copy the nat table name */
	strlcpy(ipv4_nat_cache.ip4_tbl[index].table_name,
					mem->dev_name,
					IPA_RESOURCE_NAME_MAX);
	ipv4_nat_cache.ip4_tbl[index].nat_fd = fd;

	/* open the nat device Table */
#ifndef IPA_ON_R3PC
	ipv4_rules_addr = (void *)mmap(NULL, mem->size,
																 prot, flags,
																 fd, offset);
#else
	IPADBG("user space r3pc\n");
	ipv4_rules_addr = (void *)mmap((caddr_t)0, NAT_MMAP_MEM_SIZE,
																 prot, flags,
																 fd, offset);
#endif
	if (MAP_FAILED  == ipv4_rules_addr) {
		perror("unable to mmap the memory\n");
		return -EINVAL;
	}

#ifdef IPA_ON_R3PC
	ret = ioctl(ipv4_nat_cache.ipa_fd, IPA_IOC_GET_NAT_OFFSET, &nat_mem_offset);
	if (ret != 0) {
		perror("ipa_nati_post_ipv4_init_cmd(): ioctl error value");
		IPAERR("unable to post ant offset cmd Error: %d\n", ret);
		IPADBG("ipa fd %d\n", ipv4_nat_cache.ipa_fd);
		return -EIO;
	}
	ipv4_rules_addr += nat_mem_offset;
	ipv4_nat_cache.ip4_tbl[index].mmap_offset = nat_mem_offset;
#endif

	IPADBG("mmap return value 0x%lx\n", (long unsigned int)ipv4_rules_addr);

	ipv4_nat_cache.ip4_tbl[index].ipv4_rules_addr = ipv4_rules_addr;

	ipv4_nat_cache.ip4_tbl[index].ipv4_expn_rules_addr =
	ipv4_rules_addr + (IPA_NAT_TABLE_ENTRY_SIZE * tbl_entries);

	ipv4_nat_cache.ip4_tbl[index].index_table_addr =
	ipv4_rules_addr + (IPA_NAT_TABLE_ENTRY_SIZE * (tbl_entries + expn_tbl_entries));

	ipv4_nat_cache.ip4_tbl[index].index_table_expn_addr =
	ipv4_rules_addr +
	(IPA_NAT_TABLE_ENTRY_SIZE * (tbl_entries + expn_tbl_entries))+
	(IPA_NAT_INDEX_TABLE_ENTRY_SIZE * tbl_entries);

	return 0;
}

/* comment: check the implementation once
	 offset should be in terms of byes */
int ipa_nati_post_ipv4_init_cmd(uint8_t tbl_index)
{
	struct ipa_ioc_v4_nat_init cmd;
	uint32_t offset = ipv4_nat_cache.ip4_tbl[tbl_index].tbl_addr_offset;
	int ret;

	cmd.tbl_index = tbl_index;

	cmd.ipv4_rules_offset = offset;
	cmd.expn_rules_offset = cmd.ipv4_rules_offset +
	(ipv4_nat_cache.ip4_tbl[tbl_index].table_entries * IPA_NAT_TABLE_ENTRY_SIZE);

	cmd.index_offset = cmd.expn_rules_offset +
	(ipv4_nat_cache.ip4_tbl[tbl_index].expn_table_entries * IPA_NAT_TABLE_ENTRY_SIZE);

	cmd.index_expn_offset = cmd.index_offset +
	(ipv4_nat_cache.ip4_tbl[tbl_index].table_entries * IPA_NAT_INDEX_TABLE_ENTRY_SIZE);

	cmd.table_entries  = ipv4_nat_cache.ip4_tbl[tbl_index].table_entries - 1;
	cmd.expn_table_entries = ipv4_nat_cache.ip4_tbl[tbl_index].expn_table_entries;

	cmd.ip_addr = ipv4_nat_cache.ip4_tbl[tbl_index].public_addr;

	ret = ioctl(ipv4_nat_cache.ipa_fd, IPA_IOC_V4_INIT_NAT, &cmd);
	if (ret != 0) {
		perror("ipa_nati_post_ipv4_init_cmd(): ioctl error value");
		IPAERR("unable to post init cmd Error: %d\n", ret);
		IPADBG("ipa fd %d\n", ipv4_nat_cache.ipa_fd);
		return -EINVAL;
	}
	IPADBG("Posted IPA_IOC_V4_INIT_NAT to kernel successfully\n");

	return 0;
}

int ipa_nati_del_ipv4_table(uint32_t tbl_hdl)
{
	uint8_t index = (uint8_t)(tbl_hdl - 1);
	void *addr = (void *)ipv4_nat_cache.ip4_tbl[index].ipv4_rules_addr;
	struct ipa_ioc_v4_nat_del del_cmd;
	int ret;

	if (!ipv4_nat_cache.ip4_tbl[index].valid) {
		IPAERR("invalid table handle passed\n");
		ret = -EINVAL;
		goto fail;
	}

	if (pthread_mutex_lock(&nat_mutex) != 0) {
		ret = -1;
		goto lock_mutex_fail;
	}

	/* unmap the device memory from user space */
#ifndef IPA_ON_R3PC
	munmap(addr, ipv4_nat_cache.ip4_tbl[index].size);
#else
	addr = (char *)addr - ipv4_nat_cache.ip4_tbl[index].mmap_offset;
	munmap(addr, NAT_MMAP_MEM_SIZE);
#endif

	/* close the file descriptor of nat device */
	if (close(ipv4_nat_cache.ip4_tbl[index].nat_fd)) {
		IPAERR("unable to close the file descriptor\n");
		ret = -EINVAL;
		if (pthread_mutex_unlock(&nat_mutex) != 0)
			goto unlock_mutex_fail;
		goto fail;
	}

	del_cmd.table_index = index;
	del_cmd.public_ip_addr = ipv4_nat_cache.ip4_tbl[index].public_addr;
	ret = ioctl(ipv4_nat_cache.ipa_fd, IPA_IOC_V4_DEL_NAT, &del_cmd);
	if (ret != 0) {
		perror("ipa_nati_del_ipv4_table(): ioctl error value");
		IPAERR("unable to post nat del command init Error: %d\n", ret);
		IPADBG("ipa fd %d\n", ipv4_nat_cache.ipa_fd);
		ret = -EINVAL;
		if (pthread_mutex_unlock(&nat_mutex) != 0)
			goto unlock_mutex_fail;
		goto fail;
	}
	IPAERR("posted IPA_IOC_V4_DEL_NAT to kernel successfully\n");

	free(ipv4_nat_cache.ip4_tbl[index].index_expn_table_meta);
	free(ipv4_nat_cache.ip4_tbl[index].rule_id_array);

	memset(&ipv4_nat_cache.ip4_tbl[index],
				 0,
				 sizeof(ipv4_nat_cache.ip4_tbl[index]));

	/* Decrease the table count by 1*/
	ipv4_nat_cache.table_cnt--;

	if (pthread_mutex_unlock(&nat_mutex) != 0) {
		ret = -1;
		goto unlock_mutex_fail;
	}

	return 0;

lock_mutex_fail:
	IPAERR("unable to lock the nat mutex\n");
	return ret;

unlock_mutex_fail:
	IPAERR("unable to unlock the nat mutex\n");

fail:
	return ret;
}

int ipa_nati_query_timestamp(uint32_t  tbl_hdl,
				uint32_t  rule_hdl,
				uint32_t  *time_stamp)
{
	uint8_t tbl_index = (uint8_t)(tbl_hdl - 1);
	uint8_t expn_tbl = 0;
	uint16_t tbl_entry = 0;
	struct ipa_nat_rule *tbl_ptr = NULL;

	if (!ipv4_nat_cache.ip4_tbl[tbl_index].valid) {
		IPAERR("invalid table handle\n");
		return -EINVAL;
	}

	if (pthread_mutex_lock(&nat_mutex) != 0) {
		IPAERR("unable to lock the nat mutex\n");
		return -1;
	}

	ipa_nati_parse_ipv4_rule_hdl(tbl_index, (uint16_t)rule_hdl,
															 &expn_tbl, &tbl_entry);

	tbl_ptr =
	(struct ipa_nat_rule *)ipv4_nat_cache.ip4_tbl[tbl_index].ipv4_rules_addr;
	if (expn_tbl) {
		tbl_ptr =
			 (struct ipa_nat_rule *)ipv4_nat_cache.ip4_tbl[tbl_index].ipv4_expn_rules_addr;
	}

	if (tbl_ptr)
		*time_stamp = Read32BitFieldValue(tbl_ptr[tbl_entry].ts_proto,
					TIME_STAMP_FIELD);

	if (pthread_mutex_unlock(&nat_mutex) != 0) {
		IPAERR("unable to unlock the nat mutex\n");
		return -1;
	}

	return 0;
}

int ipa_nati_modify_pdn(struct ipa_ioc_nat_pdn_entry *entry)
{
	if (entry->public_ip == 0)
		IPADBG("PDN %d public ip will be set  to 0\n", entry->pdn_index);

	if (ioctl(ipv4_nat_cache.ipa_fd, IPA_IOC_NAT_MODIFY_PDN, entry)) {
		perror("ipa_nati_modify_pdn(): ioctl error value");
		IPAERR("unable to call modify pdn icotl\n");
		IPAERR("index %d, ip 0x%X, src_metdata 0x%X, dst_metadata 0x%X\n",
			entry->pdn_index, entry->public_ip, entry->src_metadata, entry->dst_metadata);
		IPADBG("ipa fd %d\n", ipv4_nat_cache.ipa_fd);
		return -EIO;
	}

	pdns[entry->pdn_index].public_ip = entry->public_ip;
	pdns[entry->pdn_index].dst_metadata = entry->dst_metadata;
	pdns[entry->pdn_index].src_metadata = entry->src_metadata;

	IPADBG("posted IPA_IOC_NAT_MODIFY_PDN to kernel successfully and stored in cache\n index %d, ip 0x%X, src_metdata 0x%X, dst_metadata 0x%X\n",
		entry->pdn_index, entry->public_ip, entry->src_metadata, entry->dst_metadata);

	return 0;
}

int ipa_nati_add_ipv4_rule(uint32_t tbl_hdl,
				const ipa_nat_ipv4_rule *clnt_rule,
				uint32_t *rule_hdl)
{
	struct ipa_nat_ip4_table_cache *tbl_ptr;
	struct ipa_nat_sw_rule sw_rule;
	struct ipa_nat_indx_tbl_sw_rule index_sw_rule;
	uint16_t new_entry, new_index_tbl_entry;

	/* verify that the rule's PDN is valid */
	if (clnt_rule->pdn_index >= IPA_MAX_PDN_NUM ||
		pdns[clnt_rule->pdn_index].public_ip == 0) {
		IPAERR("invalid parameters, pdn index %d, public ip = 0x%X\n",
			clnt_rule->pdn_index, pdns[clnt_rule->pdn_index].public_ip);
		return -EINVAL;
	}

	memset(&sw_rule, 0, sizeof(sw_rule));
	memset(&index_sw_rule, 0, sizeof(index_sw_rule));

	/* Generate rule from client input */
	if (ipa_nati_generate_rule(tbl_hdl, clnt_rule,
					&sw_rule, &index_sw_rule,
					&new_entry, &new_index_tbl_entry)) {
		IPAERR("unable to generate rule\n");
		return -EINVAL;
	}

	tbl_ptr = &ipv4_nat_cache.ip4_tbl[tbl_hdl-1];
	ipa_nati_copy_ipv4_rule_to_hw(tbl_ptr, &sw_rule, new_entry, (uint8_t)(tbl_hdl-1));
	ipa_nati_copy_ipv4_index_rule_to_hw(tbl_ptr,
																			&index_sw_rule,
																			new_index_tbl_entry,
																			(uint8_t)(tbl_hdl-1));

	IPADBG("new entry:%d, new index entry: %d\n", new_entry, new_index_tbl_entry);
	if (ipa_nati_post_ipv4_dma_cmd((uint8_t)(tbl_hdl - 1), new_entry)) {
		IPAERR("unable to post dma command\n");
		return -EIO;
	}

	/* Generate rule handle */
	*rule_hdl  = ipa_nati_make_rule_hdl((uint16_t)tbl_hdl, new_entry);
	if (!(*rule_hdl)) {
		IPAERR("unable to generate rule handle\n");
		return -EINVAL;
	}

#ifdef NAT_DUMP
	ipa_nat_dump_ipv4_table(tbl_hdl);
#endif

	return 0;
}

int ipa_nati_generate_rule(uint32_t tbl_hdl,
				const ipa_nat_ipv4_rule *clnt_rule,
				struct ipa_nat_sw_rule *rule,
				struct ipa_nat_indx_tbl_sw_rule *index_sw_rule,
				uint16_t *tbl_entry,
				uint16_t *indx_tbl_entry)
{
	struct ipa_nat_ip4_table_cache *tbl_ptr;
	uint16_t tmp;

	if (NULL == clnt_rule || NULL == index_sw_rule ||
			NULL == rule || NULL == tbl_entry  ||
			NULL == indx_tbl_entry) {
		IPAERR("invalid parameters\n");
		return -EINVAL;
	}

	tbl_ptr = &ipv4_nat_cache.ip4_tbl[tbl_hdl-1];

	*tbl_entry = ipa_nati_generate_tbl_rule(clnt_rule,
																					rule,
																					tbl_ptr);
	if (IPA_NAT_INVALID_NAT_ENTRY == *tbl_entry) {
		IPAERR("unable to generate table entry\n");
		return -EINVAL;
	}

	index_sw_rule->tbl_entry = *tbl_entry;
	*indx_tbl_entry = ipa_nati_generate_index_rule(clnt_rule,
																								 index_sw_rule,
																								 tbl_ptr);
	if (IPA_NAT_INVALID_NAT_ENTRY == *indx_tbl_entry) {
		IPAERR("unable to generate index table entry\n");
		return -EINVAL;
	}

	rule->indx_tbl_entry = *indx_tbl_entry;
	if (*indx_tbl_entry >= tbl_ptr->table_entries) {
		tmp = *indx_tbl_entry - tbl_ptr->table_entries;
		tbl_ptr->index_expn_table_meta[tmp].prev_index = index_sw_rule->prev_index;
	}

	return 0;
}

uint16_t ipa_nati_generate_tbl_rule(const ipa_nat_ipv4_rule *clnt_rule,
						struct ipa_nat_sw_rule *sw_rule,
						struct ipa_nat_ip4_table_cache *tbl_ptr)
{
	uint32_t pub_ip_addr;
	uint16_t prev = 0, nxt_indx = 0, new_entry;
	struct ipa_nat_rule *tbl = NULL, *expn_tbl = NULL;

	pub_ip_addr = pdns[clnt_rule->pdn_index].public_ip;

	tbl = (struct ipa_nat_rule *)tbl_ptr->ipv4_rules_addr;
	expn_tbl = (struct ipa_nat_rule *)tbl_ptr->ipv4_expn_rules_addr;

	/* copy the values from client rule to sw rule */
	sw_rule-