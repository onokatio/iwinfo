#include <assert.h>
#include <stdio.h>
/* The stock SDK libnl-tiny predates const-qualified policy arguments. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#include "../iwinfo_nl80211.c"
#pragma GCC diagnostic pop

static void decode(int type, int width, unsigned int mcs, unsigned int nss,
                   struct iwinfo_rate_entry *rate)
{
	struct nl_msg *msg = nlmsg_alloc();
	struct nlattr *attrs[NL80211_RATE_INFO_MAX + 1] = {0};
	assert(msg);
	assert(!nla_put_u8(msg, type, mcs));
	assert(!nla_put_u8(msg, type == NL80211_RATE_INFO_HE_MCS ? NL80211_RATE_INFO_HE_NSS : NL80211_RATE_INFO_EHT_NSS, nss));
	assert(!nla_put_flag(msg, width));
	assert(!nla_parse(attrs, NL80211_RATE_INFO_MAX, nlmsg_data(nlmsg_hdr(msg)), nlmsg_hdr(msg)->nlmsg_len - NLMSG_HDRLEN, NULL));
	memset(rate, 0, sizeof(*rate));
	nl80211_parse_rateinfo(attrs, rate);
	nlmsg_free(msg);
}

int main(void)
{
	struct iwinfo_rate_entry rate;
	struct iwinfo_assoclist_entry station;
	struct nl80211_array_buf stations = { .buf = &station };
	struct nl80211_sta_flag_update flags = { .mask = BIT(NL80211_STA_FLAG_AUTHORIZED) };
	struct nl_msg *msg = nlmsg_alloc();
	struct nlattr *info;
	const uint8_t mac[6] = { 2, 0, 0, 0, 0, 1 };
	assert(msg && genlmsg_put(msg, 0, 0, 0, 0, 0, NL80211_CMD_NEW_STATION, 0));
	assert(!nla_put(msg, NL80211_ATTR_MAC, sizeof(mac), mac));
	info = nla_nest_start(msg, NL80211_ATTR_STA_INFO);
	assert(info && !nla_put_u32(msg, NL80211_STA_INFO_RX_PACKETS, 0));
	assert(!nla_put(msg, NL80211_STA_INFO_STA_FLAGS, sizeof(flags), &flags));
	nla_nest_end(msg, info);
	nl80211_get_assoclist_cb(msg, &stations);
	assert(stations.count == 1 && !memcmp(station.mac, mac, sizeof(mac)));
	assert(station.stats_mask == (IWINFO_ASSOC_S_VALID | IWINFO_ASSOC_S_RX_PACKETS));
	assert(station.flags_mask == IWINFO_ASSOC_F_AUTHORIZED && !station.is_authorized && !station.rx_packets);
	assert(station.rx_rate.mcs == -1 && station.tx_rate.eht_gi == UINT8_MAX);
	nlmsg_free(msg);
	decode(NL80211_RATE_INFO_HE_MCS, NL80211_RATE_INFO_80_MHZ_WIDTH, 11, 2, &rate);
	assert(rate.is_he && !rate.is_eht && rate.mcs == 11 && rate.nss == 2 && rate.mhz == 80);
	decode(NL80211_RATE_INFO_EHT_MCS, NL80211_RATE_INFO_320_MHZ_WIDTH, 15, 2, &rate);
	assert(rate.is_eht && !rate.is_he && rate.mhz_hi == 1 && rate.mhz == 64 && rate.mcs == 15 && rate.nss == 2);
	decode(NL80211_RATE_INFO_EHT_MCS, NL80211_RATE_INFO_320_MHZ_WIDTH, 255, 9, &rate);
	assert(rate.mcs == -1 && !rate.nss);
	assert(nl80211_freq2channel(2412) == 1);
	assert(nl80211_freq2channel(2484) == 14);
	assert(nl80211_freq2channel(5260) == 52);
	assert(nl80211_freq2channel(5935) == 2);
	assert(nl80211_freq2channel(5955) == 1);
	assert(nl80211_freq2channel(7115) == 233);
	puts("PASS: full nl80211 translation unit, real libnl-tiny attributes; optional station metadata, HE/EHT, 320 MHz, invalid MCS/NSS, all six channel edges");
	return 0;
}
