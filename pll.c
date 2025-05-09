/*
 * Calculate the parameters of a pll_144x based on a desired clock
 *
 * The functions in this file were copied from the linux kernel source code
 * and adapted so the code can be compiled on a PC. Having the ability to
 * calculate the parameters like this is important to add new entries to the
 * imx_pll1443x_tbl on older kernels, where the automatic PLL parameter calculation
 * was not available yet.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define USHRT_MAX       ((unsigned short)~0U)
#define SHRT_MAX        ((short)(USHRT_MAX >> 1))
#define SHRT_MIN        ((short)(-SHRT_MAX - 1))
#define LONG_MAX        ((long)(~0UL >> 1))

#define KDIV_MIN        SHRT_MIN
#define KDIV_MAX        SHRT_MAX

#define min(x, y) ({                            \
        typeof(x) _min1 = (x);                  \
        typeof(y) _min2 = (y);                  \
        (void) (&_min1 == &_min2);              \
        _min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({                            \
        typeof(x) _max1 = (x);                  \
        typeof(y) _max2 = (y);                  \
        (void) (&_max1 == &_max2);              \
        _max1 > _max2 ? _max1 : _max2; })

#define clamp(val, lo, hi)      min((typeof(val))max(val, lo), hi)
#define __clamp(val, lo, hi)    \
        ((val) >= (hi) ? (hi) : ((val) <= (lo) ? (lo) : (val)))
/**
 * clamp_t - return a value clamped to a given range using a given type
 * @type: the type of variable to use
 * @val: current value
 * @lo: minimum allowable value
 * @hi: maximum allowable value
 *
 * This macro does no typechecking and uses temporary variables of type
 * @type to make all the comparisons.
 */
#define clamp_t(type, val, lo, hi) __clamp((type)(val), (type)(lo), (type)(hi))

/**
 * do_div - returns 2 values: calculate remainder and update new dividend
 * @n: uint64_t dividend (will be updated)
 * @base: uint32_t divisor
 *
 * Summary:
 * ``uint32_t remainder = n % base;``
 * ``n = n / base;``
 *
 * Return: (uint32_t)remainder
 *
 * NOTE: macro parameter @n is evaluated multiple times,
 * beware of side effects!
 */
# define do_div(n,base) ({                                      \
        uint32_t __base = (base);                               \
        uint32_t __rem;                                         \
        __rem = ((uint64_t)(n)) % __base;                       \
        (n) = ((uint64_t)(n)) / __base;                         \
        __rem;                                                  \
 })

/*
 * Divide positive or negative dividend by positive or negative divisor
 * and round to closest integer. Result is undefined for negative
 * divisors if the dividend variable type is unsigned and for negative
 * dividends if the divisor variable type is unsigned.
 */
#define DIV_ROUND_CLOSEST(x, divisor)(                  \
{                                                       \
        typeof(x) __x = x;                              \
        typeof(divisor) __d = divisor;                  \
        (((typeof(x))-1) > 0 ||                         \
         ((typeof(divisor))-1) > 0 ||                   \
         (((__x) > 0) == ((__d) > 0))) ?                \
                (((__x) + ((__d) / 2)) / (__d)) :       \
                (((__x) - ((__d) / 2)) / (__d));        \
}                                                       \
)

struct imx_pll14xx_rate_table {                                                                                                        
        unsigned int rate;
        unsigned int pdiv;
        unsigned int mdiv;
        unsigned int sdiv;
        unsigned int kdiv;
};

static long pll14xx_calc_rate(int mdiv, int pdiv,
                              int sdiv, int kdiv, unsigned long prate)
{
        unsigned long rate = 0;
        uint64_t fout = prate;
        int i;

        /* fout = (m * 65536 + k) * Fin / (p * 65536) / (1 << sdiv) */
        fout *= ((uint64_t)mdiv * 65536 + (uint64_t)kdiv);
        pdiv *= 65536;

        do_div(fout, pdiv << sdiv);

        return rate ? (unsigned long) rate : (unsigned long)fout;
}

static long pll1443x_calc_kdiv(int mdiv, int pdiv, int sdiv,
                unsigned long rate, unsigned long prate)
{
        long kdiv;

        /* calc kdiv = round(rate * pdiv * 65536 * 2^sdiv / prate) - (mdiv * 65536) */
        kdiv = ((rate * ((pdiv * 65536) << sdiv) + prate / 2) / prate) - (mdiv * 65536);

        return clamp_t(short, kdiv, KDIV_MIN, KDIV_MAX);
}

int main (int argc, char *argv[])
{
        int mdiv, pdiv, sdiv, kdiv;
        long fvco, fout, rate_min, rate_max, dist, best = LONG_MAX;
	const struct imx_pll14xx_rate_table *tt;
	struct imx_pll14xx_rate_table tout, *t; // output

	t = &tout;
	unsigned long prate, rate;

	/* hard coded 24MHz, because this is the default on the
	 * iMX8MP. If the root clock of your tree is different,
	 * this value needs to be changed
	 */
	prate = 24000000;

	/*
	 * Just use it correctly, no effort is being done
	 * to sanitize the input or parse the arguments
	 */
	rate = strtoul(argv[1], NULL, 10);

        /*
         * Fractional PLL constrains:
         *
         * a) 1 <= p <= 63
         * b) 64 <= m <= 1023
         * c) 0 <= s <= 6
         * d) -32768 <= k <= 32767
         *
         * fvco = (m * 65536 + k) * prate / (p * 65536)
         *
         * e) 1600MHz <= fvco <= 3200MHz
         */

        for (pdiv = 1; pdiv <= 63; pdiv++) {
                for (sdiv = 0; sdiv <= 6; sdiv++) {
                        /* calc mdiv = round(rate * pdiv * 2^sdiv) / prate) */
                        mdiv = DIV_ROUND_CLOSEST(rate * (pdiv << sdiv), prate);
                        mdiv = clamp(mdiv, 64, 1023);

                        kdiv = pll1443x_calc_kdiv(mdiv, pdiv, sdiv, rate, prate);
                        fout = pll14xx_calc_rate(mdiv, pdiv, sdiv, kdiv, prate);

                        fvco = fout << sdiv;

                        if (fvco < 1600000000 || fvco > 3200000000)
                                continue;
                        /* best match */
                        dist = labs((long)rate - (long)fout);
                        if (dist < best) {
                                best = dist;
                                t->rate = (uint32_t)fout;
                                t->mdiv = mdiv;
                                t->pdiv = pdiv;
                                t->sdiv = sdiv;
                                t->kdiv = kdiv;

                                if (!dist)
                                        goto found;
                        }
                }
        }
found:
        printf("in=%ld, want=%ld got=%d (pdiv=%d sdiv=%d mdiv=%d kdiv=%d)\n",
                 prate, rate, t->rate, t->pdiv, t->sdiv,
                 t->mdiv, t->kdiv);
}
