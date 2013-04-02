// 
// mskmodem_test.c
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <math.h>
#include "liquid.h"

#define OUTPUT_FILENAME "mskmodem_test.m"

// print usage/help message
void usage()
{
    printf("mskmodem_test -- minimum-shift keying modem example\n");
    printf("options:\n");
    printf("  h     : print help\n");
    printf("  k     : samples/symbol,         default:  8\n");
    printf("  H     : modulation index,       default:  0.5\n");
    printf("  B     : filter roll-off,        default:  0.35\n");
    printf("  n     : number of data symbols, default: 80\n");
    printf("  s     : SNR [dB],               default: 20\n");
    printf("  t     : filter type: [square], rcos-full, rcos-half, gmsk\n");
}

int main(int argc, char*argv[]) {
    // options
    unsigned int k=8;                   // filter samples/symbol
    unsigned int bps=1;                 // number of bits/symbol
    float h = 0.5f;                     // modulation index (h=1/2 for MSK)
    unsigned int num_data_symbols = 20; // number of data symbols
    float SNRdB = 90.0f;                // signal-to-noise ratio [dB]
    enum {
        TXFILT_SQUARE=0,
        TXFILT_RCOS_FULL,
        TXFILT_RCOS_HALF,
        TXFILT_GMSK,
    } tx_filter_type = TXFILT_SQUARE;
    float gmsk_bt = 0.35f;              // GMSK bandwidth-time factor

    int dopt;
    while ((dopt = getopt(argc,argv,"hk:b:H:B:n:s:t:")) != EOF) {
        switch (dopt) {
        case 'h': usage();                         return 0;
        case 'k': k = atoi(optarg);                break;
        case 'b': bps = atoi(optarg);              break;
        case 'H': h = atof(optarg);                break;
        case 'B': gmsk_bt = atof(optarg);          break;
        case 'n': num_data_symbols = atoi(optarg); break;
        case 's': SNRdB = atof(optarg);            break;
        case 't':
            if (strcmp(optarg,"square")==0) {
                tx_filter_type = TXFILT_SQUARE;
            } else if (strcmp(optarg,"rcos-full")==0) {
                tx_filter_type = TXFILT_RCOS_FULL;
            } else if (strcmp(optarg,"rcos-half")==0) {
                tx_filter_type = TXFILT_RCOS_HALF;
            } else if (strcmp(optarg,"gmsk")==0) {
                tx_filter_type = TXFILT_GMSK;
            } else {
                fprintf(stderr,"error: %s, unknown filter type '%s'\n", argv[0], optarg);
                exit(1);
            }
            break;
        default:
            exit(1);
        }
    }

    unsigned int i;

    // derived values
    unsigned int num_symbols = num_data_symbols;
    unsigned int num_samples = k*num_symbols;
    unsigned int M = 1 << bps;              // constellation size
    float nstd = powf(10.0f, -SNRdB/20.0f);

    // arrays
    unsigned char sym_in[num_symbols];      // input symbols
    float phi[num_samples];                 // transmitted phase
    float complex x[num_samples];           // transmitted signal
    float complex y[num_samples];           // received signal
    float complex z[num_samples];           // output...
    //unsigned char sym_out[num_symbols];     // output symbols

    unsigned int ht_len = 0;
    unsigned int tx_delay = 0;
    float * ht = NULL;
    switch (tx_filter_type) {
    case TXFILT_SQUARE:
        // regular MSK
        ht_len = k;
        tx_delay = 1;
        ht = (float*) malloc(ht_len *sizeof(float));
        for (i=0; i<ht_len; i++)
            ht[i] = h * M_PI / (float)k;
        break;
    case TXFILT_RCOS_FULL:
        // full-response raised-cosine pulse
        ht_len = k;
        tx_delay = 1;
        ht = (float*) malloc(ht_len *sizeof(float));
        for (i=0; i<ht_len; i++)
            ht[i] = h * M_PI / (float)k * (1.0f - cosf(2.0f*M_PI*i/(float)ht_len));
        break;
    case TXFILT_RCOS_HALF:
        // partial-response raised-cosine pulse
        ht_len = 3*k;
        tx_delay = 2;
        ht = (float*) malloc(ht_len *sizeof(float));
        for (i=0; i<ht_len; i++)
            ht[i] = 0.0f;
        for (i=0; i<2*k; i++)
            ht[i+k/2] = h * 0.5f * M_PI / (float)k * (1.0f - cosf(2.0f*M_PI*i/(float)(2*k)));
        break;
    case TXFILT_GMSK:
        ht_len = 2*k*3+1+k;
        tx_delay = 4;
        ht = (float*) malloc(ht_len *sizeof(float));
        for (i=0; i<ht_len; i++)
            ht[i] = 0.0f;
        liquid_firdes_gmsktx(k,3,gmsk_bt,0.0f,&ht[k/2]);
        for (i=0; i<ht_len; i++)
            ht[i] *= h * 2.0f / (float)k;
        break;
    default:
        fprintf(stderr,"error: %s, invalid tx filter type\n", argv[0]);
        exit(1);
    }
    for (i=0; i<ht_len; i++)
        printf("ht(%3u) = %12.8f;\n", i+1, ht[i]);
    interp_rrrf interp_tx = interp_rrrf_create(k, ht, ht_len);

    // generate symbols and interpolate
    // phase-accumulating filter (trapezoidal integrator)
    float b[2] = {0.5f,  0.5f};
    if (tx_filter_type == TXFILT_SQUARE) {
        // square filter: rectangular integration with one sample of delay
        b[0] = 0.0f;
        b[1] = 1.0f;
    }
    float a[2] = {1.0f, -1.0f};
    iirfilt_rrrf integrator = iirfilt_rrrf_create(b,2,a,2);
    float theta = 0.0f;
    for (i=0; i<num_symbols; i++) {
        sym_in[i] = rand() % M;
        float v = 2.0f*sym_in[i] - (float)(M-1);    // +/-1, +/-3, ... +/-(M-1)
        interp_rrrf_execute(interp_tx, v, &phi[k*i]);

        // accumulate phase
        unsigned int j;
        for (j=0; j<k; j++) {
            iirfilt_rrrf_execute(integrator, phi[i*k+j], &theta);
            x[i*k+j] = cexpf(_Complex_I*theta);
        }
    }
    iirfilt_rrrf_destroy(integrator);

    // push through channel
    for (i=0; i<num_samples; i++)
        y[i] = x[i] + nstd*(randnf() + _Complex_I*randnf())*M_SQRT1_2;
    
    // create decimator
    unsigned int m = 3;
    float bw = 0.0f;
    firfilt_crcf decim_rx = NULL;
    if (tx_filter_type == TXFILT_SQUARE) {
        //bw = 0.9f / (float)k;
        bw = 0.4f;
        decim_rx = firfilt_crcf_create_kaiser(2*k*m+1, bw, 60.0f, 0.0f);
    } else {
        // use GMSK compensating filter for all partial-response filters
        // TODO: determine appropriate bandwidth for M-CPFSK for M > 2
        if (bps > 1) {
            bw = 1.4f / (float)k;
            m *= 2;
            decim_rx = firfilt_crcf_create_rnyquist(LIQUID_RNYQUIST_GMSKRX,k/2,m,0.3f,0);
        } else {
            bw = 0.5f / (float)k;
            decim_rx = firfilt_crcf_create_rnyquist(LIQUID_RNYQUIST_GMSKRX,k,m,0.3f,0);
        }
    }
    printf("bw = %f\n", bw);

    // run receiver
    unsigned int n=0;
    unsigned int num_errors = 0;
    unsigned int num_symbols_checked = 0;
    float complex z_prime = 0.0f;
    for (i=0; i<num_samples; i++) {
        // push through filter
        firfilt_crcf_push(decim_rx, y[i]);
        firfilt_crcf_execute(decim_rx, &z[i]);

        z[i] *= 2.0f * bw;

        // decimate output
        if ( (i%k)==0 ) {
            float phi_hat = cargf(conjf(z_prime) * z[i]);
            unsigned int sym_out = phi_hat > 0 ? 1 : 0; // estimated transmitted symbol
            z_prime = z[i];

            printf("%3u : %12.8f + j%12.8f (%1u)", n, crealf(z[i]), cimagf(z[i]), sym_out);
            if (n >= m+tx_delay) {
                num_errors += (sym_out == sym_in[n-m-tx_delay]) ? 0 : 1;
                num_symbols_checked++;
                printf(" (%1u)\n", sym_in[n-m-tx_delay]);
            } else {
                printf("\n");
            }
            n++;
        }
    }

    // print number of errors
    printf("errors : %3u / %3u\n", num_errors, num_symbols_checked);

    // destroy objects
    interp_rrrf_destroy(interp_tx);
    firfilt_crcf_destroy(decim_rx);

    // compute power spectral density
    unsigned int nfft = 1024;
    float psd[nfft];
    spgram periodogram = spgram_create_kaiser(nfft, nfft/2, 8.0f);
    spgram_estimate_psd(periodogram, y, num_samples, psd);
    spgram_destroy(periodogram);

    // 
    // export results
    //
    FILE * fid = fopen(OUTPUT_FILENAME,"w");
    fprintf(fid,"%% %s : auto-generated file\n", OUTPUT_FILENAME);
    fprintf(fid,"clear all\n");
    fprintf(fid,"close all\n");
    fprintf(fid,"k = %u;\n", k);
    fprintf(fid,"h = %f;\n", h);
    fprintf(fid,"num_symbols = %u;\n", num_symbols);
    fprintf(fid,"num_samples = %u;\n", num_samples);
    fprintf(fid,"nfft        = %u;\n", nfft);
    fprintf(fid,"delay       = %u; %% receive filter delay\n", tx_delay);

    fprintf(fid,"x   = zeros(1,num_samples);\n");
    fprintf(fid,"y   = zeros(1,num_samples);\n");
    fprintf(fid,"z   = zeros(1,num_samples);\n");
    fprintf(fid,"phi = zeros(1,num_samples);\n");
    for (i=0; i<num_samples; i++) {
        fprintf(fid,"x(%4u) = %12.8f + j*%12.8f;\n", i+1, crealf(x[i]), cimagf(x[i]));
        fprintf(fid,"y(%4u) = %12.8f + j*%12.8f;\n", i+1, crealf(y[i]), cimagf(y[i]));
        fprintf(fid,"z(%4u) = %12.8f + j*%12.8f;\n", i+1, crealf(z[i]), cimagf(z[i]));
        fprintf(fid,"phi(%4u) = %12.8f;\n", i+1, phi[i]);
    }
    // save PSD with FFT shift
    fprintf(fid,"psd = zeros(1,nfft);\n");
    for (i=0; i<nfft; i++) {
        fprintf(fid,"psd(%4u) = %12.8f;\n", i+1, psd[(i+nfft/2)%nfft]/(float)k);
    }

    fprintf(fid,"t=[0:(num_samples-1)]/k;\n");
    fprintf(fid,"i = 1:k:num_samples;\n");
    fprintf(fid,"figure;\n");
    fprintf(fid,"subplot(3,4,1:3);\n");
    fprintf(fid,"  plot(t,real(x),'-', t(i),real(x(i)),'bs','MarkerSize',4,...\n");
    fprintf(fid,"       t,imag(x),'-', t(i),imag(x(i)),'gs','MarkerSize',4);\n");
    fprintf(fid,"  axis([0 num_symbols -1.2 1.2]);\n");
    fprintf(fid,"  xlabel('time');\n");
    fprintf(fid,"  ylabel('x(t)');\n");
    fprintf(fid,"  grid on;\n");
    fprintf(fid,"subplot(3,4,5:7);\n");
    fprintf(fid,"  plot(t-delay,real(z),'-', t(i)-delay,real(z(i)),'bs','MarkerSize',4,...\n");
    fprintf(fid,"       t-delay,imag(z),'-', t(i)-delay,imag(z(i)),'gs','MarkerSize',4);\n");
    fprintf(fid,"  axis([0 num_symbols -1.2 1.2]);\n");
    fprintf(fid,"  xlabel('time');\n");
    fprintf(fid,"  ylabel('\"matched\" filter output');\n");
    fprintf(fid,"  grid on;\n");
    // plot I/Q constellations
    fprintf(fid,"subplot(3,4,4);\n");
    fprintf(fid,"  plot(real(y),imag(y),'-',real(y(i)),imag(y(i)),'rs','MarkerSize',3);\n");
    fprintf(fid,"  xlabel('I');\n");
    fprintf(fid,"  ylabel('Q');\n");
    fprintf(fid,"  axis([-1 1 -1 1]*1.2);\n");
    fprintf(fid,"  axis square;\n");
    fprintf(fid,"  grid on;\n");
    fprintf(fid,"subplot(3,4,8);\n");
    fprintf(fid,"  plot(real(z),imag(z),'-',real(z(i)),imag(z(i)),'rs','MarkerSize',3);\n");
    fprintf(fid,"  xlabel('I');\n");
    fprintf(fid,"  ylabel('Q');\n");
    fprintf(fid,"  axis([-1 1 -1 1]*1.2);\n");
    fprintf(fid,"  axis square;\n");
    fprintf(fid,"  grid on;\n");
    // plot PSD
    fprintf(fid,"f = [0:(nfft-1)]/nfft - 0.5;\n");
    fprintf(fid,"subplot(3,4,9:12);\n");
    fprintf(fid,"  plot(f,10*log10(psd),'LineWidth',1.5);\n");
    fprintf(fid,"  axis([-0.5 0.5 -60 20]);\n");
    fprintf(fid,"  xlabel('Normalized Frequency [f/F_s]');\n");
    fprintf(fid,"  ylabel('PSD [dB]');\n");
    fprintf(fid,"  grid on;\n");

#if 0
    fprintf(fid,"figure;\n");
    fprintf(fid,"  %% compute instantaneous received frequency\n");
    fprintf(fid,"  freq_rx = arg( conj(z(:)) .* circshift(z(:),-1) )';\n");
    fprintf(fid,"  freq_rx(1:(k*delay)) = 0;\n");
    fprintf(fid,"  freq_rx(end) = 0;\n");
    fprintf(fid,"  %% compute instantaneous tx/rx phase\n");
    if (tx_filter_type == TXFILT_SQUARE) {
        fprintf(fid,"  theta_tx = filter([0 1],[1 -1],phi)/(h*pi);\n");
        fprintf(fid,"  theta_rx = filter([0 1],[1 -1],freq_rx)/(h*pi);\n");
    } else {
        fprintf(fid,"  theta_tx = filter([0.5 0.5],[1 -1],phi)/(h*pi);\n");
        fprintf(fid,"  theta_rx = filter([0.5 0.5],[1 -1],freq_rx)/(h*pi);\n");
    }
    fprintf(fid,"  %% plot instantaneous tx/rx phase\n");
    fprintf(fid,"  plot(t,      theta_tx,'-b', t(i),      theta_tx(i),'sb',...\n");
    fprintf(fid,"       t-delay,theta_rx,'-r', t(i)-delay,theta_rx(i),'sr');\n");
    fprintf(fid,"  xlabel('time');\n");
    fprintf(fid,"  ylabel('instantaneous phase/(h \\pi)');\n");
    fprintf(fid,"  legend('transmitted','syms','received/filtered','syms','location','northwest');\n");
    fprintf(fid,"  grid on;\n");
#else
    // plot filter response
    fprintf(fid,"ht_len = %u;\n", ht_len);
    fprintf(fid,"ht     = zeros(1,ht_len);\n");
    for (i=0; i<ht_len; i++)
        fprintf(fid,"ht(%4u) = %12.8f;\n", i+1, ht[i]);
    fprintf(fid,"gt1 = filter([0.5 0.5],[1 -1],ht) / (pi*h);\n");
    fprintf(fid,"gt2 = filter([0.0 1.0],[1 -1],ht) / (pi*h);\n");
    fprintf(fid,"tfilt = [0:(ht_len-1)]/k - delay + 0.5;\n");
    fprintf(fid,"figure;\n");
    fprintf(fid,"plot(tfilt,ht, '-x','MarkerSize',4,...\n");
    fprintf(fid,"     tfilt,gt1,'-x','MarkerSize',4,...\n");
    fprintf(fid,"     tfilt,gt2,'-x','MarkerSize',4);\n");
    fprintf(fid,"axis([tfilt(1) tfilt(end) -0.1 1.1]);\n");
    fprintf(fid,"legend('pulse','trap. int.','rect. int.','location','northwest');\n");
    fprintf(fid,"grid on;\n");
#endif

    fclose(fid);
    printf("results written to '%s'\n", OUTPUT_FILENAME);
    
    // free allocated filter memory
    free(ht);

    return 0;
}
