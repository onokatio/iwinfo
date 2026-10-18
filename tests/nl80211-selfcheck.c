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
	puts("PASS: full nl80211 translation unit, real libnl-tiny attributes; HE/EHT, 320 MHz, invalid MCS/NSS, all six channel edges");
	return 0;
}
