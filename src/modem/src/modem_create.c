/*
 * Copyright (c) 2007, 2009 Joseph Gaeddert
 * Copyright (c) 2007, 2009 Virginia Polytechnic Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
//
//

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "liquid.internal.h"

modem modem_create(
    modulation_scheme _scheme,
    unsigned int _bits_per_symbol)
{
    if (_bits_per_symbol < 1 ) {
        fprintf(stderr,"error: modem_create(), modem must have at least 1 bit/symbol\n");
        exit(1);
    } else if (_bits_per_symbol > MAX_MOD_BITS_PER_SYMBOL) {
        fprintf(stderr,"error: modem_create(), maximum number of bits/symbol (%u) exceeded\n",
                MAX_MOD_BITS_PER_SYMBOL);
        exit(1);
    }

    switch (_scheme) {
    case MOD_PSK:
        return modem_create_psk(_bits_per_symbol);
    case MOD_DPSK:
        return modem_create_dpsk(_bits_per_symbol);
    case MOD_ASK:
        return modem_create_ask(_bits_per_symbol);
    case MOD_QAM:
        return modem_create_qam(_bits_per_symbol);
    case MOD_APSK:
        return modem_create_apsk(_bits_per_symbol);

    // arbitrary modem definitions
    case MOD_ARB:
        return modem_create_arb(_bits_per_symbol);
    case MOD_ARB_MIRRORED:
        return modem_create_arb_mirrored(_bits_per_symbol);
    case MOD_ARB_ROTATED:
        return modem_create_arb_rotated(_bits_per_symbol);

    // specific modems
    case MOD_BPSK:
        return modem_create_bpsk();
    case MOD_QPSK:
        return modem_create_qpsk();
    case MOD_APSK16:
        return modem_create_apsk16(_bits_per_symbol);
    case MOD_APSK32:
        return modem_create_apsk32(_bits_per_symbol);
    case MOD_APSK64:
        return modem_create_apsk64(_bits_per_symbol);
    default:
        fprintf(stderr,"error: modem_create(), unknown/unsupported modulation scheme : %u (%u b/s)\n",
                _scheme, _bits_per_symbol);
        exit(-1);
    }

    // should never get to this point, but adding return statment
    // to keep compiler happy
    return NULL;
}

void modem_init(modem _mod, unsigned int _bits_per_symbol)
{
    if (_bits_per_symbol < 1 ) {
        fprintf(stderr,"error: modem_init(), modem must have at least 1 bit/symbol\n");
        exit(1);
    } else if (_bits_per_symbol > MAX_MOD_BITS_PER_SYMBOL) {
        fprintf(stderr,"error: modem_init(), maximum number of bits per symbol exceeded\n");
        exit(1);
    }

    _mod->m = _bits_per_symbol;
    _mod->M = 1 << (_mod->m);
    _mod->m_i = 0;
    _mod->M_i = 0;
    _mod->m_q = 0;
    _mod->M_q = 0;

    _mod->alpha = 0.0f;

    _mod->symbol_map = NULL;

    _mod->state = 0.0f;
    _mod->state_theta = 0.0f;

    _mod->res = 0.0f;

    _mod->phase_error = 0.0f;
    _mod->evm = 0.0f;

    _mod->d_phi = 0.0f;

    _mod->modulate_func = NULL;
    _mod->demodulate_func = NULL;
}

modem modem_create_ask(
    unsigned int _bits_per_symbol)
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_ASK;

    modem_init(mod, _bits_per_symbol);

    mod->m_i = mod->m;
    mod->M_i = mod->M;

    switch (mod->M) {
    case 2:     mod->alpha = ASK2_ALPHA;     break;
    case 4:     mod->alpha = ASK4_ALPHA;     break;
    case 8:     mod->alpha = ASK8_ALPHA;     break;
    case 16:    mod->alpha = ASK16_ALPHA;    break;
    case 32:    mod->alpha = ASK32_ALPHA;    break;
    default:
        // calculate alpha dynamically
        // NOTE: this is only an approximation
        mod->alpha = sqrtf(3.0f)/(float)(mod->M);
    }

    unsigned int k;
    for (k=0; k<(mod->m); k++)
        mod->ref[k] = (1<<k) * mod->alpha;

    mod->modulate_func = &modem_modulate_ask;
    mod->demodulate_func = &modem_demodulate_ask;

    return mod;
}

modem modem_create_qam(
    unsigned int _bits_per_symbol)
{
    if (_bits_per_symbol < 1 ) {
        fprintf(stderr,"error: modem_create_qam(), modem must have at least 2 bits/symbol\n");
        exit(1);
    }

    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_QAM;

    modem_init(mod, _bits_per_symbol);

    if (mod->m % 2) {
        // rectangular qam
        mod->m_i = (mod->m + 1) >> 1;
        mod->m_q = (mod->m - 1) >> 1;
    } else {
        // square qam
        mod->m_i = mod->m >> 1;
        mod->m_q = mod->m >> 1;
    }

    mod->M_i = 1 << (mod->m_i);
    mod->M_q = 1 << (mod->m_q);

    assert(mod->m_i + mod->m_q == mod->m);
    assert(mod->M_i * mod->M_q == mod->M);

    switch (mod->M) {
    case 4:     mod->alpha = RQAM4_ALPHA;       break;
    case 8:     mod->alpha = RQAM8_ALPHA;       break;
    case 16:    mod->alpha = RQAM16_ALPHA;      break;
    case 32:    mod->alpha = RQAM32_ALPHA;      break;
    case 64:    mod->alpha = RQAM64_ALPHA;      break;
    case 128:   mod->alpha = RQAM128_ALPHA;     break;
    case 256:   mod->alpha = RQAM256_ALPHA;     break;
    case 512:   mod->alpha = RQAM512_ALPHA;     break;
    case 1024:  mod->alpha = RQAM1024_ALPHA;    break;
    case 2048:  mod->alpha = RQAM2048_ALPHA;    break;
    case 4096:  mod->alpha = RQAM4096_ALPHA;    break;
    default:
        // calculate alpha dynamically
        // NOTE: this is only an approximation
        mod->alpha = sqrtf(2.0f / (float)(mod->M) );
    }

    unsigned int k;
    for (k=0; k<(mod->m); k++)
        mod->ref[k] = (1<<k) * mod->alpha;

    mod->modulate_func = &modem_modulate_qam;
    mod->demodulate_func = &modem_demodulate_qam;

    return mod;
}

modem modem_create_psk(
    unsigned int _bits_per_symbol)
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_PSK;

    modem_init(mod, _bits_per_symbol);

    mod->alpha = M_PI/(float)(mod->M);

    unsigned int k;
    for (k=0; k<(mod->m); k++)
        mod->ref[k] = (1<<k) * mod->alpha;

    mod->d_phi = M_PI*(1.0f - 1.0f/(float)(mod->M));

    mod->modulate_func = &modem_modulate_psk;
    mod->demodulate_func = &modem_demodulate_psk;

    return mod;
}

modem modem_create_bpsk()
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_BPSK;

    modem_init(mod, 1);

    mod->modulate_func = &modem_modulate_bpsk;
    mod->demodulate_func = &modem_demodulate_bpsk;

    return mod;
}

modem modem_create_qpsk()
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_QPSK;

    modem_init(mod, 2);

    mod->modulate_func = &modem_modulate_qpsk;
    mod->demodulate_func = &modem_demodulate_qpsk;

    return mod;
}

modem modem_create_dpsk(
    unsigned int _bits_per_symbol)
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_DPSK;

    modem_init(mod, _bits_per_symbol);

    mod->alpha = M_PI/(float)(mod->M);

    unsigned int k;
    for (k=0; k<(mod->m); k++)
        mod->ref[k] = (1<<k) * mod->alpha;

    mod->d_phi = M_PI*(1.0f - 1.0f/(float)(mod->M));

    mod->state = 1.0f;
    mod->state_theta = 0.0f;

    mod->modulate_func = &modem_modulate_dpsk;
    mod->demodulate_func = &modem_demodulate_dpsk;

    return mod;
}

modem modem_create_apsk(
    unsigned int _bits_per_symbol)
{
    switch (_bits_per_symbol) {
    case 4:     return modem_create_apsk16(_bits_per_symbol);
    case 5:     return modem_create_apsk32(_bits_per_symbol);
    case 6:     return modem_create_apsk64(_bits_per_symbol);
    default:
        fprintf(stderr,"error: modem_create_apsk(), unsupported modulation level (%u)\n",
                _bits_per_symbol);
        exit(1);
    }

    return NULL;
}

modem modem_create_apsk16(
    unsigned int _bits_per_symbol)
{
    if (_bits_per_symbol != 4) {
        fprintf(stderr,"error: modem_create_apsk16(), bits/symbol is not exactly 4\n");
        exit(1);
    }
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_APSK16;

    modem_init(mod, 4);
    
    // set internals
    mod->apsk_num_levels = apsk16_num_levels;
    mod->apsk_p = (unsigned int *) apsk16_p;
    mod->apsk_r = (float *) apsk16_r;
    mod->apsk_phi = (float *) apsk16_phi;
    mod->apsk_r_slicer = (float *) apsk16_r_slicer;
    mod->apsk_symbol_map = (unsigned int *) apsk16_symbol_map;

    mod->modulate_func = &modem_modulate_apsk;
    mod->demodulate_func = &modem_demodulate_apsk;

    return mod;
}

modem modem_create_apsk32(
    unsigned int _bits_per_symbol)
{
    if (_bits_per_symbol != 5) {
        fprintf(stderr,"error: modem_create_apsk32(), bits/symbol is not exactly 5\n");
        exit(1);
    }
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_APSK32;

    modem_init(mod, 5);
    
    // set internals
    mod->apsk_num_levels = apsk32_num_levels;
    mod->apsk_p = (unsigned int *) apsk32_p;
    mod->apsk_r = (float *) apsk32_r;
    mod->apsk_phi = (float *) apsk32_phi;
    mod->apsk_r_slicer = (float *) apsk32_r_slicer;
    mod->apsk_symbol_map = (unsigned int *) apsk32_symbol_map;

    mod->modulate_func = &modem_modulate_apsk;
    mod->demodulate_func = &modem_demodulate_apsk;

    return mod;
}

modem modem_create_apsk64(
    unsigned int _bits_per_symbol)
{
    if (_bits_per_symbol != 6) {
        fprintf(stderr,"error: modem_create_apsk64(), bits/symbol is not exactly 6\n");
        exit(1);
    }
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_APSK64;

    modem_init(mod, 6);
    
    // set internals
    mod->apsk_num_levels = apsk64_num_levels;
    mod->apsk_p = (unsigned int *) apsk64_p;
    mod->apsk_r = (float *) apsk64_r;
    mod->apsk_phi = (float *) apsk64_phi;
    mod->apsk_r_slicer = (float *) apsk64_r_slicer;
    mod->apsk_symbol_map = (unsigned int *) apsk64_symbol_map;

    mod->modulate_func = &modem_modulate_apsk;
    mod->demodulate_func = &modem_demodulate_apsk;

    return mod;
}

modem modem_create_arb(
    unsigned int _bits_per_symbol)
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_ARB;

    modem_init(mod, _bits_per_symbol);

    mod->M = mod->M;
    mod->symbol_map = (float complex*) calloc( mod->M, sizeof(float complex) );

    mod->modulate_func = &modem_modulate_arb;
    mod->demodulate_func = &modem_demodulate_arb;

    return mod;
}

modem modem_create_arb_mirrored(
    unsigned int _bits_per_symbol)
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_ARB_MIRRORED;

    modem_init(mod, _bits_per_symbol);

    /// \bug
    mod->M = (mod->M) >> 2;    // 2^(m-2) = M/4
    mod->symbol_map = (float complex*) calloc( mod->M, sizeof(float complex) );

    mod->modulate_func = &modem_modulate_arb;
    mod->demodulate_func = &modem_demodulate_arb;

    return mod;
}

modem modem_create_arb_rotated(
    unsigned int _bits_per_symbol)
{
    modem mod = (modem) malloc( sizeof(struct modem_s) );
    mod->scheme = MOD_ARB_ROTATED;

    modem_init(mod, _bits_per_symbol);

    /// \bug
    mod->M = (mod->M) >> 2;    // 2^(m-2) = M/4
    mod->symbol_map = (float complex*) calloc( mod->M, sizeof(float complex) );

    mod->modulate_func = &modem_modulate_arb;
    mod->demodulate_func = &modem_demodulate_arb;

    return mod;
}

void modem_arb_init(modem _mod, float complex *_symbol_map, unsigned int _len)
{
#ifdef LIQUID_VALIDATE_INPUT
    if ( (_mod->scheme != MOD_ARB) && (_mod->scheme != MOD_ARB_MIRRORED) &&
         (_mod->scheme != MOD_ARB_ROTATED) )
    {
        fprintf(stderr,"error: modem_arb_init(), modem is not of arbitrary type\n");
        exit(1);
    } else if (_len != _mod->M) {
        fprintf(stderr,"error: modem_arb_init(), array sizes do not match\n");
        exit(1);
    }
#endif

    unsigned int i;
    for (i=0; i<_len; i++) {
#ifdef LIQUID_VALIDATE_INPUT
        if ((_mod->scheme == MOD_ARB_MIRRORED) || (_mod->scheme == MOD_ARB_ROTATED)) {
            // symbols should only exist in first quadrant
            if ( crealf(_symbol_map[i]) <= 0 || cimagf(_symbol_map[i]) <= 0 )
                printf("WARNING: modem_arb_init(), symbols exist outside first quadrant\n");
        }
#endif

        _mod->symbol_map[i] = _symbol_map[i];
    }

    // balance I/Q channels
    if (_mod->scheme == MOD_ARB)
        modem_arb_balance_iq(_mod);

    // scale modem to have unity energy
    modem_arb_scale(_mod);

}

void modem_arb_init_file(modem _mod, char* filename) {
    // try to open file
    FILE * f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr,"modem_arb_init_file(), could not open file\n");
        exit(1);
    }

    unsigned int i, results;
    float sym_i, sym_q;
    for (i=0; i<_mod->M; i++) {
        results = fscanf(f, "%f %f\n", &sym_i, &sym_q);
        _mod->symbol_map[i] = sym_i + _Complex_I*sym_q;

        // ensure proper number of symbols were read
        if (results < 2) {
            fprintf(stderr,"modem_arb_init_file() unable to parse line\n");
            exit(-1);
        }

#ifdef LIQUID_VALIDATE_INPUT
        if ((_mod->scheme == MOD_ARB_MIRRORED) || (_mod->scheme == MOD_ARB_ROTATED)) {
        // symbols should only exist in first quadrant
            if ( sym_i < 0.0f || sym_q < 0.0f )
                printf("WARNING: modem_arb_init_file(), symbols exist outside first quadrant\n");
        }
#endif

    }

    fclose(f);

    // balance I/Q channels
    if (_mod->scheme == MOD_ARB)
        modem_arb_balance_iq(_mod);

    // scale modem to have unity energy
    modem_arb_scale(_mod);
}

void modem_arb_scale(modem _mod)
{
    unsigned int i;

    // calculate energy
    float mag, e = 0.0f;
    for (i=0; i<_mod->M; i++) {
        mag = cabsf(_mod->symbol_map[i]);
        e += mag*mag;
    }

    e = sqrtf( e / _mod->M );

    for (i=0; i<_mod->M; i++) {
        _mod->symbol_map[i] /= e;
    }
}

void modem_arb_balance_iq(modem _mod)
{
    float mean=0.0f;
    unsigned int i;

    // accumulate average signal
    for (i=0; i<_mod->M; i++) {
        mean += _mod->symbol_map[i];
    }
    mean /= (float) (_mod->M);

    // subtract mean value from reference levels
    for (i=0; i<_mod->M; i++) {
        _mod->symbol_map[i] -= mean;
    }
}

void modem_destroy(modem _mod)
{
    free(_mod->symbol_map);
    free(_mod);
}

void modem_print(modem _mod)
{
    printf("linear modem:\n");
    printf("    scheme:         %s\n", modulation_scheme_str[_mod->scheme]);
    printf("    bits/symbol:    %u\n", _mod->m);
}


