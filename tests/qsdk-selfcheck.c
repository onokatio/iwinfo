/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include "../iwinfo_qcawificfg80211.c"
static int nl_rate;
static int native_bitrate(const char *name, int *buf) { *buf = nl_rate; return 0; }
static int vendor_bitrate(const char *name, int *buf) { *buf = 2882; return 0; }
const struct iwinfo_ops nl80211_ops = { .bitrate = native_bitrate };
const struct iwinfo_ops wext_ops = { .bitrate = vendor_bitrate };

int main(void)
{
	int hw, ht;
	assert(qca_name("wifi2") && qca_name("ath26"));
	assert(!qca_name("../etc/passwd") && !qca_name("ath2;id") && !qca_name(""));
	assert(!qca_name("abcdefghijklmnop"));
	assert(!qca_bitrate("ath-test", &hw) && hw == 2882000);
	nl_rate = 1200000;
	assert(!qca_bitrate("ath-test", &hw) && hw == 1200000);
	qca_parse_modes("11AXA_HE20 11AXA_HE160 11BEA_EHT20 11BEA_EHT320", &hw, &ht);
	assert((hw & (IWINFO_80211_AX | IWINFO_80211_BE)) == (IWINFO_80211_AX | IWINFO_80211_BE));
	assert(ht & IWINFO_HTMODE_EHT320);
	assert(!(ht & IWINFO_HTMODE_EHT160));
	assert(!(hw & IWINFO_80211_B));
	qca_parse_modes("11B 11G 11NG_HT40PLUS 11BEG_EHT40MINUS", &hw, &ht);
	assert(hw & IWINFO_80211_B);
	assert(ht == (IWINFO_HTMODE_HT40 | IWINFO_HTMODE_EHT40));
	return 0;
}
