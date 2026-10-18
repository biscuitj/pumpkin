// Pixel-for-pixel parity with include/companion.h is verified in test_companions.py.
(function(scope) {
    'use strict';
    const names=['Pip the goldfish','Boo the ghost','Mochi the axolotl','Sprout the dinosaur','Pebble the turtle','Inky the octopus','Maple the red panda','Patch the pumpkin','Toffee the cat','Kiko the frog','Domino the penguin','Hoot the owl','Leo the lion','Stella the unicorn','Kobi the crab'];
    const palette={a:'#f06460',r:'#f06460',i:'#94e6a1',u:'#263b32',W:'#d1fff1',M:'#86dfd1',S:'#4b9d9d',E:'#0b1725',O:'#ff9d48',Y:'#ffd47c',R:'#e57439',P:'#ffc8d9',G:'#ec779e',L:'#94e6a1',D:'#50ad7c',H:'#ff83ac',B:'#74bfdc',C:'#fff1d7',V:'#c997ff',T:'#8f5c47',F:'#5fd35f',J:'#e4f59a',N:'#3d62d6',U:'#b8743f',Q:'#f3d9a8',A:'#ffd98a',K:'#d9822b',X:'#ffe36b',Z:'#8fb8ff',c:'#f2554a',k:'#b8322b'};
    const valid=id=>Number.isInteger(id)&&id>=0&&id<names.length;
    const validStyle=style=>Number.isInteger(style)&&style>=0&&style<3;
    const fish=['.....YY......','....OOOOO..Y.','..YOOOOOOOYY.','.OOOEOOOOOYYY','..OOOOOOOOYY.','...RRRORR..Y.','......Y......'];
    const ghost=['...WWW...','..WWWWW..','.WWWWWWW.','.WEWWEWW.','.WWWWWWW.','.MWWEWWM.','.MM.M.MM.'];
    const shortGhost=['...WWW...','..WWWWW..','.WEWWEWW.','.WWWWWWW.','.MWWEWWM.','.MM.M.MM.'];
    const axolotl=['.G.......G.','..GPPPPPG..','G.PPPPPPP.G','.GPEPPPEPG.','G.PGPEPGP.G','...PPPPP...','..P.....P..'];
    const dino=[
        ['............','......LLLLL.','.....LLLELLL','.....LLLLLLL','L...DLLLL...','LL.DLLLLLL..','.LLLLLYL....','...LL.LL....'],
        ['............','............','......LLLLL.','.....LLLELLL','L...DLLLLLLL','LLLDLLLLL...','..LLLYLLLL..','...LL.LL....'],
        ['......LLLLL.','.....LLLELLL','.....LLLLLLL','....DLLLL...','L..DLLLLLL..','LL.LLLYL....','.LLLLLLL....','....L..L....']
    ];
    const newcomers=[[["..............","....LLLL......","...LDLLDL.....","..LDDLDDDL.LL.","..LDLLLDLL.LEL",".LLLLLLLLLLLL.","...L....L.....","..LL...LL....."],["..............","..............","....LLLL......","...LDLLDL.....","..LDDLDDDL....",".LLLLLLLLLLEL.","...L....L.....","..LL...LL....."],["..............","....LLLL..LL..","...LDLLDL.LEL.","..LDDLDDDLLLL.","..LDLLLDLL.L..",".LLLLLLLLLLL..","...L....L.....","...LL...LL...."]],[["....VVVV......","...VVVVVV.....","..VVVVVVVV....","..VCEVVCEV....","..VVVVVVVV....","...VVVVVV.....","..VV.VV.VV....",".VV..VV..VV..."],["..............","....VVVV......","...VVVVVV.....","..VVVVVVVV....","..VCEVVCEV....","..VVVVVVVV....","...VVVVVV.....","..VV.VV.VV...."],["....VVVV......","...VVVVVV.....","..VVVVVVVV....","..VCEVVCEV....","V.VVVVVVVV.V..","VV.VVVVVV.VV..","..VV.VV.VV....",".VV..VV..VV..."]],[[".O...O........",".OCCCO........","OOOOOOO.......","OCECECO....OO.","OCCYCCO...OYY.",".OOOOO...YYOO.",".TTTTT.OOYY...",".TT.TT.OO....."],["..............",".O...O........",".OCCCO........","OOOOOOO.......","OCECECO.......","OCCYCCO....OO.",".OOOOO.OOYYOO.",".TT.TT.YYOO..."],[".O...O.....OO.",".OCCCO....OYY.","OOOOOOO...YYO.","OCECECO...OO..","OCCYCCO..YY...","OOOOOOO..OO...",".TTTTT.OO.....",".TT.TT.OO....."]]];
    const pumpkin=['...OOOROOO','..OROOOOORO','.OOROOOOOROO','.OOROOOOOROO','.OOROOOOOROO','..OROOOOORO','...OOOOOOO'];
    const squashedPumpkin=['...OOOROOO','..OROOOOORO','.OOROOOOOROO','.OOROOOOOROO','.OOROOOOOROO','..OOOOOOOOO'];
    const font={L:['100','100','100','100','111'],O:['111','101','101','101','111'],W:['101','101','101','111','101'],K:['101','101','110','101','101'],H:['101','101','111','101','101'],I:['111','010','010','010','111'],G:['111','100','101','101','111']};
    // Toffee, Kiko, Domino, Hoot, Leo, Stella, Kobi (ids 8-14): mirrors companion_friend().
    const friends={
        cat:['..O.....O.....','..OO...OO.....','..OOOROOO.....','..OEOOOEO.....','.WOOOHOOOW....','...OOOOO...O..','..OOCCCOO..O..','..OCC.CCOOO...'],
        catLow:['..............','.OO.....OO....','..OOOROOO.....','..OOOOOOO.....','.WOEOHOEOW....','..OOOOOOO.....','..OCCCCCO.....','..OC.C.COOOOO.'],
        frog:['.WWW.....WWW..','.WEWFFFFFWEW..','..FFFFFFFFF...','.FGFFFFFFFGF..','.FFEFFFFFEFF..','..FFEEEEEFF...','..FFJJJJJFF...','.FF.F...F.FF..'],
        penguin:['.....NNNN.....','....NNNNNN....','....NEWWEN....','....NWOOWN....','...NNWWWWNN...','...N.WWWW.N...','....NWWWWN....','.....OO.OO....'],
        penguinLow:['..............','.....NNNN.....','....NNNNNN....','....NWWWWN....','....NEOOEN....','...NNWWWWNN...','...NNWWWWNN...','.....OO.OO....'],
        owl:['..UU.....UU...','...UUUUUUU....','..UQQQUQQQU...','..UQEQUQEQU...','..UQQQYQQQU...','..UUUUYUUUU...','...UQUQUQU....','....YY.YY.....'],
        lion:['...KKKKKK.....','..KKAKKAKK....','.KKAAAAAAKK...','.KAAEAAEAAK...','KKAAATTAAAKK..','.KAACCCCAAK...','..KAACCAAK....','...KKKKKK.....'],
        unicorn:['......W....X..','......WW..Y...','.....WWWWX....','.....WWEWWW...','.....WWWGWWWP.','.....WWWWWWP..','.....WWW......','....WWWW......'],
        crab:['.c.c.W..W.c.c.','.ccc.k..k.ccc.','..c..k..k..c..','..cccccccccc..','..cccccccccc..','..cGcEccEcGc..','..kcccEEccck..','.k.k.k..k.k.k.'],
        crabLow:['..............','.....W..W.....','.....E..E.....','...cccccccc...','.cccccccccccc.','.ccccGcEEcGcc.','..kccEccEcck..','.k.k.k..k.k.k.']
    };
    const rainbow='HOYLBV';
    const maneUnicorn=[[5,1],[4,2],[4,3],[3,3],[4,4],[3,4],[4,5],[3,5],[4,6],[3,6],[3,7],[2,7]];
    function friend(id,ms,phase,sleepy,happy,range,pet,put){
        const load=rows=>rows.forEach((row,y)=>[...row].forEach((c,x)=>{if(x<14)pet[y][x]=c;}));
        const zzz=(x,y)=>{const step=Math.floor(ms/900)%3;put(x+step,y-step,'Z');};
        const low=range===1,high=range===2;
        if(id===8){
            if(low){load(friends.catLow);if(phase){put(12,7,'.');put(12,6,'O');}return;}
            load(friends.cat);
            if(high){
                put(3,4,'E');put(7,4,'E');put(5,5,'E');
                for(let y=2;y<5;y++)put(11,y,'O');
                put(12,2,'O');put(12,3,'O');
                if(phase){put(12,2,'.');put(12,1,'O');put(11,1,'O');put(3,7,'.');put(4,7,'.');put(2,6,'C');put(3,6,'C');}
                return;
            }
            if(phase){put(11,5,'.');put(12,4,'O');}
            if(sleepy){put(3,3,'R');put(7,3,'R');zzz(11,2);}
            if(happy){put(9,4,'O');put(9,5,'O');put(phase?10:9,3,'C');}
        }else if(id===9){
            load(friends.frog);
            const closed=(left,right)=>{for(let x=0;x<3;x++){if(left){put(1+x,0,'F');put(1+x,1,'D');}if(right){put(9+x,0,'F');put(9+x,1,'D');}}};
            if(low){
                for(const x of [1,2,3,9,10,11])put(x,0,'F');
                put(2,1,'E');put(10,1,'E');put(3,4,'F');put(9,4,'F');put(3,6,'E');put(9,6,'E');
                if(phase)closed(true,true);
            }else if(high){
                put(2,1,'W');put(10,1,'W');put(2,0,'E');put(10,0,'E');
                put(4,6,'E');put(5,6,'H');put(6,6,'H');put(7,6,'H');put(8,6,'E');
                if(phase){put(0,3,'F');put(0,2,'F');put(12,3,'F');put(12,2,'F');}
                else{put(0,4,'F');put(12,4,'F');}
            }else if(sleepy){closed(true,true);zzz(11,3);}
            else if(happy){closed(false,true);put(12,4,'F');put(phase?13:12,3,'F');}
            else if(phase){put(2,1,'W');put(3,1,'E');put(10,1,'W');put(11,1,'E');}
        }else if(id===10){
            if(low){load(friends.penguinLow);if(phase){put(5,4,'W');put(8,4,'W');}return;}
            load(friends.penguin);
            if(high){
                put(6,4,'O');put(7,4,'O');put(3,5,'.');put(10,5,'.');
                if(phase){put(3,4,'.');put(10,4,'.');put(3,3,'N');put(2,2,'N');put(10,3,'N');put(11,2,'N');}
                else{put(2,4,'N');put(11,4,'N');}
            }else if(sleepy){put(5,2,'W');put(8,2,'W');put(5,3,'E');put(8,3,'E');zzz(11,2);}
            else if(happy){put(10,5,'.');put(11,phase?3:4,'N');put(10,4,'N');}
            else if(phase){put(5,7,'.');put(9,7,'.');}
        }else if(id===11){
            load(friends.owl);
            if(low){
                put(2,0,'.');put(3,0,'.');put(9,0,'.');put(10,0,'.');put(2,1,'U');put(10,1,'U');
                for(const x of [3,4,5,7,8,9])put(x,2,'U');
                put(4,3,'Q');put(8,3,'Q');put(4,4,'E');put(8,4,'E');
                if(phase){put(1,5,'U');put(11,5,'U');}
            }else if(high){
                put(3,0,'U');put(9,0,'U');put(6,5,'E');
                const y=phase?2:4,tip=phase?1:5;
                put(1,y,'U');put(0,tip,'U');put(11,y,'U');put(12,tip,'U');
            }else if(sleepy){for(const x of [3,4,5,7,8,9])put(x,3,'E');zzz(11,2);}
            else if(happy){for(const x of [7,8,9])put(x,3,'E');put(11,4,'U');put(12,phase?2:3,'U');}
            else if(phase){put(4,3,'Q');put(8,3,'Q');put(3,3,'E');put(7,3,'E');}
        }else if(id===12){
            load(friends.lion);
            if(low){
                put(4,1,'K');put(7,1,'K');put(4,3,'T');put(7,3,'T');put(5,6,'E');put(6,6,'E');
                put(0,4,'.');put(11,4,'.');
                if(phase){put(1,6,'K');put(10,6,'K');}
            }else if(high){
                put(5,5,'E');put(6,5,'E');put(5,6,'H');put(6,6,'H');
                const spikes=phase?[[0,1],[11,1],[0,7],[11,7],[5,0],[6,0]]:[[1,0],[10,0],[0,6],[11,6],[0,2],[11,2]];
                for(const [x,y] of spikes)put(x,y,'K');
            }else if(sleepy){put(4,3,'T');put(7,3,'T');zzz(11,3);}
            else if(happy){put(4,3,'T');put(7,3,'T');put(5,6,'H');put(6,6,'H');if(phase){put(4,1,'K');put(7,1,'K');}}
            else if(phase){put(0,4,'.');put(11,4,'.');put(0,3,'K');put(11,3,'K');}
        }else if(id===13){
            load(friends.unicorn);
            const flow=low?0:phase+(happy?2*(Math.floor(ms/220)%3):0);
            const paint=(x,y)=>put(x,y,rainbow[(y+flow)%6]);
            for(const [x,y] of maneUnicorn)paint(x,y);
            if(low){
                const tmp=pet.map(row=>row.slice());
                pet.forEach(row=>row.fill('.'));
                for(let y=0;y<7;y++)for(let x=0;x<14;x++)pet[y+1][x]=tmp[y][x];
                put(7,4,'W');put(7,5,'E');
                for(let y=5;y<8;y++)put(2,y,rainbow[y%6]);
            }else if(high){
                const extra=phase?[[2,3],[1,4],[2,4],[1,5]]:[[2,4],[2,5],[1,6],[2,6]];
                for(const [x,y] of extra)paint(x,y);
                put(7,2,'E');
            }else if(sleepy){zzz(11,3);}
        }else if(id===14){
            const claws=(openLeft,openRight)=>{put(2,0,openLeft?'.':'c');put(11,0,openRight?'.':'c');};
            const legsStep=()=>{for(const x of [1,3,5,6,7,8,10,12])put(x,7,'.');for(const x of [2,4,9,11])put(x,7,'k');};
            if(low){load(friends.crabLow);if(phase){put(5,1,'c');put(8,1,'c');put(5,2,'k');put(8,2,'k');}return;}
            load(friends.crab);
            if(high){
                put(6,5,'E');put(7,5,'E');put(6,6,'H');put(7,6,'H');
                claws(!phase,!phase);
                if(phase){put(6,1,'B');put(7,0,'B');legsStep();}
                else{put(7,2,'B');put(6,0,'B');}
            }else if(sleepy){
                claws(false,false);
                put(5,0,'.');put(8,0,'.');put(5,1,'c');put(8,1,'c');
                const step=Math.floor(ms/900)%2;put(6+step,1-step,'Z');
            }else if(happy){put(8,0,'k');claws(phase,phase);if(phase)legsStep();}
            else{claws(!phase,phase);if(phase)legsStep();}
        }
    }
    // Styles: 0=text, 1=range icon, 2=centered pet. Ranges: 0=in range, 1=low, 2=high.
    function frame(id,ms,sleepy,happy,style=0,range=0) {
        if(!valid(id))id=0;
        if(!validStyle(style))style=0;
        if(!Number.isInteger(range)||range<0||range>2)range=0;
        ms=ms>>>0;
        sleepy=sleepy&&range===0&&!happy;
        happy=happy&&range===0;
        const pet=Array.from({length:8},()=>Array(14).fill('.'));
        const pixels=Array.from({length:8},()=>Array(32).fill('.'));
        const put=(x,y,c)=>{if(x>=0&&x<14&&y>=0&&y<8)pet[y][x]=c;};
        const sprite=(rows,sx,sy)=>rows.forEach((row,y)=>[...row].forEach((c,x)=>{if(c!=='.')put(sx+x,sy+y,c);}));
        const phase=sleepy?0:Math.floor(ms/(happy?220:range===1?1400:range===2?450:650))%2;
        if(id===0) {
            const sy=sleepy||range===1?1:range===2?0:Math.floor(ms/1600)%2;
            sprite(fish,0,sy);
            if(phase){put(12,sy+3,'.');put(11,sy,'Y');put(11,sy+6,'Y');}
            if(sleepy){put(4,sy+3,'O');put(4,sy+4,'E');}
        }else if(id===1) {
            const rows=range===0?ghost:shortGhost;
            const sy=sleepy?1:range===1?2:range===2?0:Math.floor(ms/1400)%2;
            sprite(rows,3,sy);
            for(let x=1;x<8;x++)put(3+x,sy+rows.length-1,(x+phase)%3===0?'.':'M');
            if(range===2||happy){put(3,sy+3,'W');put(11,sy+3,'W');put(2,sy+(phase?2:3),'W');put(12,sy+(phase?2:3),'W');}
            if(sleepy)for(const x of [5,8]){put(x,sy+3,'W');put(x,sy+4,'E');}
        }else if(id===2) {
            const sy=sleepy||range===1?1:0;
            sprite(axolotl,2,sy);
            if(range===1){
                for(const [x,y] of [[1,0],[9,0],[0,2],[10,2],[0,4],[10,4]])put(2+x,sy+y,'.');
                if(phase){put(3,sy+3,'.');put(11,sy+3,'.');put(4,sy+2,'G');put(10,sy+2,'G');}
            }else if(range===2||happy){
                put(2,0,'G');put(12,0,'G');put(1,phase?1:3,'G');put(13,phase?1:3,'G');
                if(phase){put(3,0,'.');put(11,0,'.');}
            }else if(phase){put(3,0,'.');put(11,0,'.');put(2,1,'G');put(12,1,'G');}
            if(sleepy)for(const x of [5,9]){put(x,sy+3,'P');put(x,sy+4,'E');}
        }else if(id===3){
            sprite(dino[range],1,0);
            if(phase){
                if(range===1){put(1,4,'.');put(1,5,'L');put(2,5,'.');put(2,6,'L');}
                else if(range===0){put(4,7,'.');put(8,7,'.');put(6,7,'D');put(9,7,'D');}
                else{put(5,7,'.');put(8,7,'.');put(6,7,'L');put(9,7,'L');}
            }
            if(sleepy){put(9,2,'L');put(9,3,'E');}
        }else if(id===7){
            if(range===1){
                sprite(squashedPumpkin,0,2);
                put(6,1,'D');put(phase?5:7,1,'L');
                put(4,4,'E');put(8,4,'E');
                put(6,5,'E');put(5,6,'E');put(7,6,'E');
            }else{
                sprite(pumpkin,0,1);
                put(6,0,'D');put(phase?8:7,0,'L');
                for(const x of [4,8]){if(sleepy)put(x,3,'E');else{put(x,2,'E');put(x,3,'E');}}
                put(3,4,'G');put(9,4,'G');
                if(range===2){
                    put(6,4,'E');put(6,5,'E');
                    for(const x of [0,12]){put(x,2,'L');put(x,phase?1:3,'L');}
                }else{
                    put(5,5,'E');put(7,5,'E');put(6,6,'E');
                    if(happy){put(0,5,'L');put(0,6,'L');put(12,2,'L');if(phase)put(12,1,'L');else put(13,1,'L');}
                }
            }
        }else if(id>=8){
            friend(id,ms,phase,sleepy,happy,range,pet,put);
        }else{
            sprite(newcomers[id-4][range],0,0);
            if(id===4){
                if(phase){put(2,7,'.');put(7,7,'.');put(4,7,'L');put(9,7,'L');}
                if(sleepy){put(12,4,'L');put(12,5,'E');}
                if(happy){put(10,6,'L');put(11,phase?5:6,'L');}
            }else if(id===5){
                if(phase){put(1,7,'.');put(10,7,'.');put(3,7,'V');put(8,7,'V');if(range===1){put(2,7,'.');put(9,7,'.');}}
                if(sleepy)for(const x of [4,8]){put(x,3,'V');put(x,4,'E');}
                if(happy){put(0,4,'V');put(11,4,'V');put(1,phase?3:5,'V');put(10,phase?3:5,'V');}
            }else{
                if(phase){put(7,7,'.');put(8,7,'.');put(9,7,'O');put(10,6,'Y');}
                if(sleepy)for(const x of [2,4]){put(x,3,'C');put(x,4,'E');}
                if(happy){put(0,phase?4:5,'O');put(6,phase?4:5,'O');}
            }
        }
        let left=14,right=-1;
        pet.forEach(row=>row.forEach((c,x)=>{if(c!=='.'){left=Math.min(left,x);right=Math.max(right,x);}}));
        const offset=Math.floor(((style===2?32:14)-(right-left+1))/2)-left;
        pet.forEach((row,y)=>row.forEach((c,x)=>{if(c!=='.')pixels[y][x+offset]=c;}));
        const color=range===1?'a':range===2?'r':'i';
        if(style===0){
            const word=range===1?'LOW':range===2?'HIGH':'OK';
            const start=15+Math.floor((17-(word.length*4-1))/2);
            [...word].forEach((c,i)=>font[c].forEach((row,y)=>[...row].forEach((bit,x)=>{if(bit==='1')pixels[y+2][start+i*4+x]=color;})));
        }else if(style===1){
            const lit=range===1?6:range===2?0:3;
            for(const y of [0,3,6])for(let x=23;x<27;x++){pixels[y][x]=y===lit?color:'u';if(y===lit)pixels[y+1][x]=color;}
        }
        return pixels;
    }
    function resolveColor(key, colors) {
        const setting={a:'color_low',i:'color_in_range',r:'color_high'}[key];
        if(colors && colors.ambient_use_glucose_colors && setting && /^#[0-9a-f]{6}$/i.test(colors[setting])) return colors[setting];
        return palette[key]||'#07090d';
    }
    function draw(canvas,id,ms,mood,style=0,range=0,colors=null) {
        const pixels = frame(id,ms,mood==='sleepy',mood==='happy',style,range);
        const rgb = new Uint8Array(768);
        pixels.forEach((row,y)=>row.forEach((c,x)=>{
            const hex = c === '.' ? '#000000' : resolveColor(c,colors);
            for (let channel=0;channel<3;channel++) rgb[(y*32+x)*3+channel]=parseInt(hex.slice(1+channel*2,3+channel*2),16);
        }));
        scope.MatrixDisplay.draw(canvas,rgb);
    }
    scope.PixelCompanions={names,palette,valid,validStyle,frame,resolveColor,draw};
})(globalThis);
