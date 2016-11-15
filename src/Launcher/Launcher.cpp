#include <cmath>
#include <thread>
#include <chrono>
#include <cassert>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <functional>

#include "Tools/Display/bash_tools.h"

#include "Launcher.hpp"

template <typename B, typename R, typename Q>
Launcher<B,R,Q>
::Launcher(const int argc, const char **argv, std::ostream &stream)
: max_n_chars(0), simu(nullptr), ar(argc, argv), stream(stream)
{
	// define type names
	type_names[typeid(char)]        = "char ("        + std::to_string(sizeof(char)*8)        + " bits)";
	type_names[typeid(signed char)] = "signed char (" + std::to_string(sizeof(signed char)*8) + " bits)";
	type_names[typeid(short)]       = "short ("       + std::to_string(sizeof(short)*8)       + " bits)";
	type_names[typeid(int)]         = "int ("         + std::to_string(sizeof(int)*8)         + " bits)";
	type_names[typeid(long long)]   = "long long ("   + std::to_string(sizeof(long long)*8)   + " bits)";
	type_names[typeid(float)]       = "float ("       + std::to_string(sizeof(float)*8)       + " bits)";
	type_names[typeid(double)]      = "double ("      + std::to_string(sizeof(double)*8)      + " bits)";

	// default parameters
	params.simulation .snr_step          = 0.1f;
	params.simulation .n_threads         = std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1;
	params.simulation .stop_time         = std::chrono::seconds(0);
	params.simulation .inter_frame_level = 1;
	params.simulation .seed              = 0;
	params.code       .tail_length       = 0;
	params.source     .type              = "RAND";
	params.source     .path              = "";
	params.modulator  .type              = "BPSK";
	params.modulator  .bits_per_symbol   = 1;
	params.modulator  .upsample_factor   = 1;
	params.modulator  .complex           = true;
	params.demodulator.max               = "MAXSS";
	params.demodulator.no_sig2           = false;
	params.channel    .domain            = "LLR";
	params.channel    .type              = "AWGN";
	params.channel    .path              = "";
	params.channel    .block_fading      = "NO";
#ifdef MIPP_NO_INTRINSICS
	params.quantizer  .type              = "STD";
#else
	params.quantizer  .type              = (typeid(R) == typeid(double)) ? "STD" : "STD_FAST";
#endif
	params.quantizer  .range             = 0.f;
	params.terminal   .disabled          = false;
	params.terminal   .frequency         = std::chrono::milliseconds(500);
}

template <typename B, typename R, typename Q>
Launcher<B,R,Q>
::~Launcher() 
{
	if (simu != nullptr) delete simu;
}

template <typename B, typename R, typename Q>
void Launcher<B,R,Q>
::build_args()
{
	// ---------------------------------------------------------------------------------------------------- simulation
	req_args[{"sim-snr-min", "m"}] =
		{"float",
		 "minimal signal/noise ratio to simulate."};
	req_args[{"sim-snr-max", "M"}] =
		{"float",
		 "maximal signal/noise ratio to simulate."};
	opt_args[{"sim-snr-step", "s"}] =
		{"positive_float",
		 "signal/noise ratio step between each simulation."};
	opt_args[{"sim-type"}] =
		{"string",
		 "select the type of simulation to launch (default is BFER).",
		 "BFER, BFERI, EXIT, GEN"};
	opt_args[{"sim-stop-time"}] =
		{"positive_int",
		 "time in sec after what the current SNR iteration should stop."};
#ifndef STARPU
	opt_args[{"sim-threads", "t"}] =
		{"positive_int",
		 "enable multi-threaded mode and specify the number of threads."};
#endif
	opt_args[{"sim-domain"}] =
		{"string",
		 "choose the domain in which you want to compute.",
		 "LR, LLR"};
#ifdef MULTI_PREC
	opt_args[{"sim-prec", "p"}] =
		{"positive_int",
		 "the simulation precision in bit.",
		 "8, 16, 32, 64"};
#endif
	this->opt_args[{"sim-inter-lvl"}] =
		{"positive_int",
		 "set the number of inter frame level to process in each modules."};
	this->opt_args[{"sim-seed"}] =
		{"positive_int",
		 "seed used in the simulation to initialize the pseudo random generators in general."};

	// ---------------------------------------------------------------------------------------------------------- code
	req_args[{"cde-type"}] =
		{"string",
		 "select the code type you want to use.",
		 "POLAR, TURBO, LDPC, REPETITION, RA, RSC, UNCODED" };
	req_args[{"cde-info-bits", "K"}] =
		{"positive_int",
		 "useful number of bit transmitted (only information bits)."};
	req_args[{"cde-size", "N"}] =
		{"positive_int",
		 "total number of bit transmitted (includes parity bits)."};

	// -------------------------------------------------------------------------------------------------------- source
	opt_args[{"src-type"}] =
		{"string",
		 "method used to generate the codewords.",
		 "RAND, RAND_FAST, AZCW, USER"};
	opt_args[{"src-path"}] =
		{"string",
		 "path to a file containing one or a set of pre-computed source bits, to use with \"--src-type USER\"."};

	// ----------------------------------------------------------------------------------------------------- modulator
	opt_args[{"mod-type"}] =
		{"string",
		 "type of the modulation to use in the simulation.",
		 "BPSK, BPSK_FAST, PSK, PAM, QAM, GSM, GSM_TBLESS, USER"};
	opt_args[{"mod-bps"}] =
		{"positive_int",
		 "select the number of bits per symbol (default is 1)."};
	opt_args[{"mod-ups"}] =
		{"positive_int",
		 "select the symbol upsample factor (default is 1)."};
	opt_args[{"mod-const-path"}] =
		{"string",
		 "path to the ordered modulation symbols (constellation), to use with \"--mod-type USER\"."};

	// --------------------------------------------------------------------------------------------------- demodulator
	opt_args[{"dmod-max"}] =
		{"string",
		 "select the type of the max operation to use in the demodulation.",
		 "MAX, MAXL, MAXS, MAXSS"};
	opt_args[{"dmod-no-sig2"}] =
		{"",
		 "turn off the division by sigma square in the demodulation."};

	// ------------------------------------------------------------------------------------------------------- channel
	std::string chan_avail = "NO, USER, AWGN, AWGN_FAST, RAYLEIGH";
#ifdef CHANNEL_GSL
	chan_avail += ", AWGN_GSL";
#endif 
#ifdef CHANNEL_MKL
	chan_avail += ", AWGN_MKL";
#endif
	opt_args[{"chn-type"}] =
		{"string",
		 "type of the channel to use in the simulation.",
		 chan_avail};
	opt_args[{"chn-path"}] =
		{"string",
		 "path to a noisy file, to use with \"--chn-type USER\"."};

	opt_args[{"chn-blk-fad"}] =
		{"string",
		 "block fading policy.",
		 "NO, FRAME, ONETAP"};

	// ----------------------------------------------------------------------------------------------------- quantizer
	if ((typeid(Q) != typeid(float)) && (typeid(Q) != typeid(double)))
	{
		opt_args[{"qnt-type"}] =
			{"string",
			 "type of the quantizer to use in the simulation.",
			 "STD, STD_FAST, TRICKY"};
		opt_args[{"qnt-dec"}] =
			{"positive_int",
			 "the position of the fixed point in the quantified representation."};
		opt_args[{"qnt-bits"}] =
			{"positive_int",
			 "the number of bits used for the quantizer."};
		opt_args[{"qnt-range"}] =
			{"positive_float",
			 "the min/max bound for the tricky quantizer."};
	}

	// ------------------------------------------------------------------------------------------------------- decoder
	opt_args[{"dec-type", "D"}] =
		{"string",
		 "select the algorithm you want to decode the codeword."};
	opt_args[{"dec-implem"}] =
		{"string",
		 "select the implementation of the algorithm to decode."};

	// ------------------------------------------------------------------------------------------------------ terminal
	opt_args[{"term-no"}] =
		{"",
		 "disable reporting for each iteration."};
	opt_args[{"term-freq"}] =
		{"positive_int",
		 "display frequency in ms (refresh time step for each iteration, 0 = disable display refresh)."};

	// --------------------------------------------------------------------------------------------------------- other
	opt_args[{"version", "v"}] =
		{"",
		 "print informations about the version of the code."};
	opt_args[{"help", "h"}] =
		{"",
		 "print this help."};
}

template <typename B, typename R, typename Q>
void Launcher<B,R,Q>
::store_args()
{
	using namespace std::chrono;

	// ---------------------------------------------------------------------------------------------------- simulation
	params.simulation.snr_min = ar.get_arg_float({"sim-snr-min", "m"}); // required
	params.simulation.snr_max = ar.get_arg_float({"sim-snr-max", "M"}); // required

	if(ar.exist_arg({"sim-type"         })) params.simulation.type              = ar.get_arg      ({"sim-type"         });
	if(ar.exist_arg({"sim-snr-step", "s"})) params.simulation.snr_step          = ar.get_arg_float({"sim-snr-step", "s"});
	if(ar.exist_arg({"sim-domain"       })) params.channel.domain               = ar.get_arg      ({"sim-domain"       });
	if(ar.exist_arg({"sim-inter-lvl"    })) params.simulation.inter_frame_level = ar.get_arg_int  ({"sim-inter-lvl"    });
	if(ar.exist_arg({"sim-stop-time"    })) params.simulation.stop_time = seconds(ar.get_arg_int  ({"sim-stop-time"    }));
	if(ar.exist_arg({"sim-seed"         })) params.simulation.seed              = ar.get_arg_int  ({"sim-seed"         });
#ifndef STARPU
	if(ar.exist_arg({"sim-threads",  "t"})) params.simulation.n_threads         = ar.get_arg_int  ({"sim-threads",  "t"});
#endif

	// ---------------------------------------------------------------------------------------------------------- code
	params.code.type   = ar.get_arg    ({"cde-type"          }); // required
	params.code.K      = ar.get_arg_int({"cde-info-bits", "K"}); // required
	params.code.N      = ar.get_arg_int({"cde-size",      "N"}); // required
	params.code.N_code = ar.get_arg_int({"cde-size",      "N"});
	params.code.m      = (int)std::ceil(std::log2(params.code.N));
	if (params.code.K > params.code.N)
	{
		std::cerr << bold_red("(EE) K have to be smaller than N, exiting.") << std::endl;
		std::exit(EXIT_FAILURE);
	}

	// -------------------------------------------------------------------------------------------------------- source
	if(ar.exist_arg({"src-type"})) params.source.type = ar.get_arg({"src-type"});
	if (params.source.type == "AZCW")
		params.encoder.type = "AZCW";
	if(ar.exist_arg({"src-path"})) params.source.path = ar.get_arg({"src-path"});

	// ----------------------------------------------------------------------------------------------------- modulator
	if(ar.exist_arg({"mod-type"})) params.modulator.type = ar.get_arg({"mod-type"});
	if (params.modulator.type == "USR" && !(ar.exist_arg({"mod-const-path"})))
	{
		std::cerr << bold_red("(EE) When USR modulation type is used, a path to a file containing the constellation")
		          << bold_red("symbols must be given.") << std::endl;
		std::exit(EXIT_FAILURE);
	}
	if (params.modulator.type.find("BPSK") != std::string::npos || params.modulator.type == "PAM")
		params.modulator.complex = false;

	if(ar.exist_arg({"mod-bps"       })) params.modulator.bits_per_symbol = ar.get_arg_int({"mod-bps"       });
	if(ar.exist_arg({"mod-ups"       })) params.modulator.upsample_factor = ar.get_arg_int({"mod-ups"       });
	if(ar.exist_arg({"mod-const-path"})) params.modulator.const_path      = ar.get_arg    ({"mod-const-path"});

	// force the number of bits per symbol to 1 when BPSK mod
	if (params.modulator.type == "BPSK" || params.modulator.type == "BPSK_FAST")
		params.modulator.bits_per_symbol = 1;

	if (params.modulator.type == "GSM" || params.modulator.type == "GSM_TBLESS")
	{
		params.modulator.bits_per_symbol = 1;
		params.modulator.upsample_factor = 5;
		params.demodulator.no_sig2 = true;
	}

	// --------------------------------------------------------------------------------------------------- demodulator
	if(ar.exist_arg({"dmod-no-sig2"})) params.demodulator.no_sig2 = true;
	if(ar.exist_arg({"dmod-max"    })) params.demodulator.max     = ar.get_arg({"dmod-max"});

	// ------------------------------------------------------------------------------------------------------- channel
	if(ar.exist_arg({"chn-type"}))    params.channel.type         = ar.get_arg({"chn-type"});
	if(ar.exist_arg({"chn-path"}))    params.channel.path         = ar.get_arg({"chn-path"});
	if(ar.exist_arg({"chn-blk-fad"})) params.channel.block_fading = ar.get_arg({"chn-blk-fad"});

	// ----------------------------------------------------------------------------------------------------- quantizer
	if ((typeid(Q) != typeid(float)) && (typeid(Q) != typeid(double)))
	{
		if(ar.exist_arg({"qnt-type" })) params.quantizer.type       = ar.get_arg      ({"qnt-type" });
		if(ar.exist_arg({"qnt-dec"  })) params.quantizer.n_decimals = ar.get_arg_int  ({"qnt-dec"  });
		if(ar.exist_arg({"qnt-bits" })) params.quantizer.n_bits     = ar.get_arg_int  ({"qnt-bits" });
		if(ar.exist_arg({"qnt-range"})) params.quantizer.range      = ar.get_arg_float({"qnt-range"});
	}

	// ------------------------------------------------------------------------------------------------------- decoder
	if(ar.exist_arg({"dec-type",  "D"})) params.decoder.type   = ar.get_arg({"dec-type",  "D"});
	if(ar.exist_arg({"dec-implem"    })) params.decoder.implem = ar.get_arg({"dec-implem"    });

	// ------------------------------------------------------------------------------------------------------ terminal
	if(ar.exist_arg({"term-no"  })) params.terminal.disabled = true;
	if(ar.exist_arg({"term-freq"})) params.terminal.frequency = milliseconds(ar.get_arg_int({"term-freq"}));
}

template <typename B, typename R, typename Q>
void Launcher<B,R,Q>
::read_arguments()
{
	this->build_args();

	opt_args[{"help", "h"}] =
		{"",
		 "print this help."};

	auto display_help = true;
	if (ar.parse_arguments(req_args, opt_args))
	{
		this->store_args();

		display_help = false;

		// print usage if there is "-h" or "--help" on the command line
		if(ar.exist_arg({"help", "h"})) display_help = true;
	}

	if (display_help)
	{
		std::vector<std::vector<std::string>> arg_grp;
		arg_grp.push_back({"sim",  "Simulation parameter(s)" });
		arg_grp.push_back({"cde",  "Code parameter(s)"       });
		arg_grp.push_back({"src",  "Source parameter(s)"     });
		arg_grp.push_back({"crc",  "CRC parameter(s)"        });
		arg_grp.push_back({"itl",  "Interleaver parameter(s)"});
		arg_grp.push_back({"pct",  "Puncturer parameter(s)"  });
		arg_grp.push_back({"dpct", "Depuncturer parameter(s)"});
		arg_grp.push_back({"enc",  "Encoder parameter(s)"    });
		arg_grp.push_back({"mod",  "Modulator parameter(s)"  });
		arg_grp.push_back({"dmod", "Demodulator parameter(s)"});
		arg_grp.push_back({"chn",  "Channel parameter(s)"    });
		arg_grp.push_back({"qnt",  "Quantizer parameter(s)"  });
		arg_grp.push_back({"dec",  "Decoder parameter(s)"    });
		arg_grp.push_back({"mnt",  "Monitor parameter(s)"    });
		arg_grp.push_back({"term", "Terminal parameter(s)"   });

		ar.print_usage(arg_grp);
		exit(EXIT_FAILURE);
	}

	if (!ar.check_arguments())
		std::exit(EXIT_FAILURE);
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_simulation()
{
	std::vector<std::pair<std::string,std::string>> p;

	p.push_back(std::make_pair("Type",          params.simulation.type));
	p.push_back(std::make_pair("SNR min (m)",   std::to_string(params.simulation.snr_min)  + " dB"));
	p.push_back(std::make_pair("SNR max (M)",   std::to_string(params.simulation.snr_max)  + " dB"));
	p.push_back(std::make_pair("SNR step (s)",  std::to_string(params.simulation.snr_step) + " dB"));
	p.push_back(std::make_pair("Type of bits",  type_names[typeid(B)]));
	p.push_back(std::make_pair("Type of reals", type_names[typeid(R)]));
	if ((typeid(Q) != typeid(float)) && (typeid(Q) != typeid(double)))
		p.push_back(std::make_pair("Type of quant. reals", type_names[typeid(Q)]));
	p.push_back(std::make_pair("Inter frame level", std::to_string(params.simulation.inter_frame_level)));
	p.push_back(std::make_pair("Seed", std::to_string(params.simulation.seed)));

	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_code()
{
	std::vector<std::pair<std::string,std::string>> p;

	std::string N = std::to_string(params.code.N);
	if (params.code.tail_length > 0)
		N += " + " + std::to_string(params.code.tail_length) + " (tail bits)";

	p.push_back(std::make_pair("Type",              params.code.type             ));
	p.push_back(std::make_pair("Info. bits (K)",    std::to_string(params.code.K)));
	p.push_back(std::make_pair("Codeword size (N)", N                            ));

	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_source()
{
	std::vector<std::pair<std::string,std::string>> p;

	p.push_back(std::make_pair("Type", params.source.type));

	if (params.source.type == "USER")
		p.push_back(std::make_pair("Path", params.source.path));

	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_crc()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_encoder()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_puncturer()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_interleaver()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_modulator()
{
	std::vector<std::pair<std::string,std::string>> p;

	std::string modulation = std::to_string((int)(1 << params.modulator.bits_per_symbol)) + "-" + params.modulator.type;
	if ((params.modulator.type == "BPSK") || (params.modulator.type == "BPSK_FAST")|| (params.modulator.type == "GSM") ||
		(params.modulator.type == "GSM_TBLESS"))
		modulation = params.modulator.type;
	if (params.modulator.upsample_factor > 1)
		modulation += " (" + std::to_string(params.modulator.upsample_factor) + "-UPS)";

	p.push_back(std::make_pair("Type", modulation));

	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_channel()
{
	std::vector<std::pair<std::string,std::string>> p;

	p.push_back(std::make_pair("Type",   params.channel.type  ));
	p.push_back(std::make_pair("Domain", params.channel.domain));

	if (params.channel.type == "USER")
		p.push_back(std::make_pair("Path", params.channel.path));

	if (params.channel.type.find("RAYLEIGH") != std::string::npos)
		p.push_back(std::make_pair("Block fading policy", params.channel.block_fading));

	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_demodulator()
{
	std::vector<std::pair<std::string,std::string>> p;

	std::string demod_sig2 = (params.demodulator.no_sig2) ? "off" : "on";
	std::string demod_max  = (params.modulator.type == "BPSK") ||
							   (params.modulator.type == "BPSK_FAST") ?
							   "unused" : params.demodulator.max;

	p.push_back(std::make_pair("Sigma square", demod_sig2));
	p.push_back(std::make_pair("Max type",     demod_max ));

	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_depuncturer()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_quantizer()
{
	std::vector<std::pair<std::string,std::string>> p;

	if ((typeid(Q) != typeid(float)) && (typeid(Q) != typeid(double)))
	{
		std::string quantif = "unused";
		if (type_names[typeid(R)] != type_names[typeid(Q)])
		{
			if (params.quantizer.type == "TRICKY")
				quantif = "{"+std::to_string(params.quantizer.n_bits)+", "+std::to_string(params.quantizer.range)+"f}";
			else
				quantif = "{"+std::to_string(params.quantizer.n_bits)+", "+std::to_string(params.quantizer.n_decimals)+"}";
		}

		p.push_back(std::make_pair("Type"               , params.quantizer.type));
		p.push_back(std::make_pair("Fixed-point config.", quantif              ));
	}

	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_decoder()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_monitor()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
std::vector<std::pair<std::string,std::string>> Launcher<B,R,Q>
::header_terminal()
{
	std::vector<std::pair<std::string,std::string>> p;
	return p;
}

template <typename B, typename R, typename Q>
void Launcher<B,R,Q>
::compute_max_n_chars()
{
	std::function<void (std::vector<std::pair<std::string,std::string>>)> compute_max =
		[&](std::vector<std::pair<std::string,std::string>> params)
	{
		for (auto i = 0; i < (int)params.size(); i++)
			this->max_n_chars = std::max(this->max_n_chars, (int)params[i].first.length());
	};

	compute_max(header_simulation ());
	compute_max(header_code       ());
	compute_max(header_source     ());
	compute_max(header_crc        ());
	compute_max(header_encoder    ());
	compute_max(header_puncturer  ());
	compute_max(header_interleaver());
	compute_max(header_modulator  ());
	compute_max(header_channel    ());
	compute_max(header_demodulator());
	compute_max(header_depuncturer());
	compute_max(header_quantizer  ());
	compute_max(header_decoder    ());
	compute_max(header_monitor    ());
	compute_max(header_terminal   ());
}

template <typename B, typename R, typename Q>
void Launcher<B,R,Q>
::print_parameters(std::string grp_name, std::vector<std::pair<std::string,std::string>> params)
{
	stream << "# * " << bold_underlined(grp_name) << " ";
	for (auto i = 0; i < 46 - (int)grp_name.length(); i++) std::cout << "-";
	stream << std::endl;

	for (auto i = 0; i < (int)params.size(); i++)
	{
		stream << "#    ** " << bold(params[i].first);
		for (auto j = 0; j < this->max_n_chars - (int)params[i].first.length(); j++) stream << " ";
		stream << " = " << params[i].second << std::endl;
	}
}

template <typename B, typename R, typename Q>
void Launcher<B,R,Q>
::print_header()
{
	this->compute_max_n_chars();

	// display configuration and simulation parameters
	stream << "# " << bold("-------------------------------------------------") << std::endl;
	stream << "# " << bold("---- A FAST FORWARD ERROR CORRECTION TOOL >> ----") << std::endl;
	stream << "# " << bold("-------------------------------------------------") << std::endl;
	stream << "# " << bold_underlined("Parameters") << ":" << std::endl;

	std::vector<std::pair<std::string,std::string>> params;
	params = this->header_simulation();  if (params.size()) this->print_parameters("Simulation",  params);
	params = this->header_code();        if (params.size()) this->print_parameters("Code",        params);
	params = this->header_source();      if (params.size()) this->print_parameters("Source",      params);
	params = this->header_crc();         if (params.size()) this->print_parameters("CRC",         params);
	params = this->header_encoder();     if (params.size()) this->print_parameters("Encoder",     params);
	params = this->header_puncturer();   if (params.size()) this->print_parameters("Puncturer",   params);
	params = this->header_interleaver(); if (params.size()) this->print_parameters("Interleaver", params);
	params = this->header_modulator();   if (params.size()) this->print_parameters("Modulator",   params);
	params = this->header_channel();     if (params.size()) this->print_parameters("Channel",     params);
	params = this->header_demodulator(); if (params.size()) this->print_parameters("Demodulator", params);
	params = this->header_depuncturer(); if (params.size()) this->print_parameters("Depuncturer", params);
	params = this->header_quantizer();   if (params.size()) this->print_parameters("Quantizer",   params);
	params = this->header_decoder();     if (params.size()) this->print_parameters("Decoder",     params);
	params = this->header_monitor();     if (params.size()) this->print_parameters("Monitor",     params);
	params = this->header_terminal();    if (params.size()) this->print_parameters("Terminal",    params);
	stream << "#" << std::endl;
}

template <typename B, typename R, typename Q>
void Launcher<B,R,Q>
::launch()
{
	std::srand(params.simulation.seed);

	// in case of the user call launch multiple times
	if (simu != nullptr)
	{
		delete simu;
		simu = nullptr;
	}

	this->read_arguments();
	this->print_header();
	simu = this->build_simu();

	if (simu != nullptr)
	{
		// launch the simulation
		stream << "# The simulation is running..." << std::endl;
		simu->launch();
		stream << "# End of the simulation." << std::endl;
	}
}

// ==================================================================================== explicit template instantiation 
#include "Tools/types.h"
#ifdef MULTI_PREC
template class Launcher<B_8,R_8,Q_8>;
template class Launcher<B_16,R_16,Q_16>;
template class Launcher<B_32,R_32,Q_32>;
template class Launcher<B_64,R_64,Q_64>;
#else
template class Launcher<B,R,Q>;
#endif
// ==================================================================================== explicit template instantiation
