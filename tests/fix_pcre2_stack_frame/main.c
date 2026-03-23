#include <stdio.h>

/*
 * Regression test for fix_pcre2_stack_frame_bloat.
 *
 * This test models pcre2's compile_branch deeply recursive pattern.
 * pcre2 crashed with a segfault at 792 nested parentheses because CCC
 * allocated 10,320-byte stack frames (vs GCC's ~500 bytes). The bloat
 * came from using 8-byte minimum stack slots for ALL SSA temporaries,
 * even 32-bit int types.
 *
 * The fix allows 4-byte stack slots for small integer/float IR types
 * (I8, U8, I16, U16, I32, U32, F32), significantly reducing per-frame
 * stack usage.
 *
 * This function declares 200 individual int local variables (NOT an
 * array) to create ~200 SSA values each requiring a stack slot, then
 * recurses 800 times. Without the fix, the stack would overflow.
 */

static int process_branch(int depth) {
    int v0=depth+0, v1=depth+1, v2=depth+2, v3=depth+3, v4=depth+4, v5=depth+5, v6=depth+6, v7=depth+7, v8=depth+8, v9=depth+9;
    int v10=depth+10, v11=depth+11, v12=depth+12, v13=depth+13, v14=depth+14, v15=depth+15, v16=depth+16, v17=depth+17, v18=depth+18, v19=depth+19;
    int v20=depth+20, v21=depth+21, v22=depth+22, v23=depth+23, v24=depth+24, v25=depth+25, v26=depth+26, v27=depth+27, v28=depth+28, v29=depth+29;
    int v30=depth+30, v31=depth+31, v32=depth+32, v33=depth+33, v34=depth+34, v35=depth+35, v36=depth+36, v37=depth+37, v38=depth+38, v39=depth+39;
    int v40=depth+40, v41=depth+41, v42=depth+42, v43=depth+43, v44=depth+44, v45=depth+45, v46=depth+46, v47=depth+47, v48=depth+48, v49=depth+49;
    int v50=depth+50, v51=depth+51, v52=depth+52, v53=depth+53, v54=depth+54, v55=depth+55, v56=depth+56, v57=depth+57, v58=depth+58, v59=depth+59;
    int v60=depth+60, v61=depth+61, v62=depth+62, v63=depth+63, v64=depth+64, v65=depth+65, v66=depth+66, v67=depth+67, v68=depth+68, v69=depth+69;
    int v70=depth+70, v71=depth+71, v72=depth+72, v73=depth+73, v74=depth+74, v75=depth+75, v76=depth+76, v77=depth+77, v78=depth+78, v79=depth+79;
    int v80=depth+80, v81=depth+81, v82=depth+82, v83=depth+83, v84=depth+84, v85=depth+85, v86=depth+86, v87=depth+87, v88=depth+88, v89=depth+89;
    int v90=depth+90, v91=depth+91, v92=depth+92, v93=depth+93, v94=depth+94, v95=depth+95, v96=depth+96, v97=depth+97, v98=depth+98, v99=depth+99;
    int v100=depth+100, v101=depth+101, v102=depth+102, v103=depth+103, v104=depth+104, v105=depth+105, v106=depth+106, v107=depth+107, v108=depth+108, v109=depth+109;
    int v110=depth+110, v111=depth+111, v112=depth+112, v113=depth+113, v114=depth+114, v115=depth+115, v116=depth+116, v117=depth+117, v118=depth+118, v119=depth+119;
    int v120=depth+120, v121=depth+121, v122=depth+122, v123=depth+123, v124=depth+124, v125=depth+125, v126=depth+126, v127=depth+127, v128=depth+128, v129=depth+129;
    int v130=depth+130, v131=depth+131, v132=depth+132, v133=depth+133, v134=depth+134, v135=depth+135, v136=depth+136, v137=depth+137, v138=depth+138, v139=depth+139;
    int v140=depth+140, v141=depth+141, v142=depth+142, v143=depth+143, v144=depth+144, v145=depth+145, v146=depth+146, v147=depth+147, v148=depth+148, v149=depth+149;
    int v150=depth+150, v151=depth+151, v152=depth+152, v153=depth+153, v154=depth+154, v155=depth+155, v156=depth+156, v157=depth+157, v158=depth+158, v159=depth+159;
    int v160=depth+160, v161=depth+161, v162=depth+162, v163=depth+163, v164=depth+164, v165=depth+165, v166=depth+166, v167=depth+167, v168=depth+168, v169=depth+169;
    int v170=depth+170, v171=depth+171, v172=depth+172, v173=depth+173, v174=depth+174, v175=depth+175, v176=depth+176, v177=depth+177, v178=depth+178, v179=depth+179;
    int v180=depth+180, v181=depth+181, v182=depth+182, v183=depth+183, v184=depth+184, v185=depth+185, v186=depth+186, v187=depth+187, v188=depth+188, v189=depth+189;
    int v190=depth+190, v191=depth+191, v192=depth+192, v193=depth+193, v194=depth+194, v195=depth+195, v196=depth+196, v197=depth+197, v198=depth+198, v199=depth+199;

    int sum = v0 ^ v1 ^ v2 ^ v3 ^ v4 ^ v5 ^ v6 ^ v7 ^ v8 ^ v9
            ^ v10 ^ v11 ^ v12 ^ v13 ^ v14 ^ v15 ^ v16 ^ v17 ^ v18 ^ v19
            ^ v20 ^ v21 ^ v22 ^ v23 ^ v24 ^ v25 ^ v26 ^ v27 ^ v28 ^ v29
            ^ v30 ^ v31 ^ v32 ^ v33 ^ v34 ^ v35 ^ v36 ^ v37 ^ v38 ^ v39
            ^ v40 ^ v41 ^ v42 ^ v43 ^ v44 ^ v45 ^ v46 ^ v47 ^ v48 ^ v49
            ^ v50 ^ v51 ^ v52 ^ v53 ^ v54 ^ v55 ^ v56 ^ v57 ^ v58 ^ v59
            ^ v60 ^ v61 ^ v62 ^ v63 ^ v64 ^ v65 ^ v66 ^ v67 ^ v68 ^ v69
            ^ v70 ^ v71 ^ v72 ^ v73 ^ v74 ^ v75 ^ v76 ^ v77 ^ v78 ^ v79
            ^ v80 ^ v81 ^ v82 ^ v83 ^ v84 ^ v85 ^ v86 ^ v87 ^ v88 ^ v89
            ^ v90 ^ v91 ^ v92 ^ v93 ^ v94 ^ v95 ^ v96 ^ v97 ^ v98 ^ v99
            ^ v100 ^ v101 ^ v102 ^ v103 ^ v104 ^ v105 ^ v106 ^ v107 ^ v108 ^ v109
            ^ v110 ^ v111 ^ v112 ^ v113 ^ v114 ^ v115 ^ v116 ^ v117 ^ v118 ^ v119
            ^ v120 ^ v121 ^ v122 ^ v123 ^ v124 ^ v125 ^ v126 ^ v127 ^ v128 ^ v129
            ^ v130 ^ v131 ^ v132 ^ v133 ^ v134 ^ v135 ^ v136 ^ v137 ^ v138 ^ v139
            ^ v140 ^ v141 ^ v142 ^ v143 ^ v144 ^ v145 ^ v146 ^ v147 ^ v148 ^ v149
            ^ v150 ^ v151 ^ v152 ^ v153 ^ v154 ^ v155 ^ v156 ^ v157 ^ v158 ^ v159
            ^ v160 ^ v161 ^ v162 ^ v163 ^ v164 ^ v165 ^ v166 ^ v167 ^ v168 ^ v169
            ^ v170 ^ v171 ^ v172 ^ v173 ^ v174 ^ v175 ^ v176 ^ v177 ^ v178 ^ v179
            ^ v180 ^ v181 ^ v182 ^ v183 ^ v184 ^ v185 ^ v186 ^ v187 ^ v188 ^ v189
            ^ v190 ^ v191 ^ v192 ^ v193 ^ v194 ^ v195 ^ v196 ^ v197 ^ v198 ^ v199;

    if (depth <= 0)
        return sum & 0xFF;
    return sum + process_branch(depth - 1);
}

int main(void) {
    (void)process_branch(800);
    printf("ok\n");
    return 0;
}
