#include <iostream>
#include <cstdlib>
#include <pthread.h>
#include <stdlib.h>
#include <cstdio>
#include <fstream>
#include <semaphore.h>
#include <vector>

using namespace std;
#define NUM_THREADS 5
const int tamanoMemoria = 1600;
const int tamanoInstrucciones = 768;
//registro 32 == RL
const int numRegistros = 33;

struct IfId{
	int npc;
	int ir[4];
};

struct IdEx{
   	int npc;
   	int a;
   	int b;
   	int immm;
   	int ir[4];
};

struct ExMem{
	int cond;
	int aluOutput;
	int b;
	int ir[4];
};

struct MemWb{
	int aluOutput;
	int lmd;
	int ir[4];
};

struct Cache{
    int bloques[8][4];
    int etiqueta[8];
    char estado[8];
};

bool ingresarInstruccionesIF = true;
bool ingresarInstruccionesID = true;
bool ingresarInstruccionesEx = true;
bool conflictoDatos = false;
int ciclo = 0;
int quantum = 0;
int pc = 0;
int etapasFinalizadas = 0;
int barrera = 4;
int hilosFinalizados = 0;
vector<int> memoria(tamanoMemoria, 1);
//registros inicializados en 1
//RL es el reg en la pos 31
vector<int> registros(numRegistros,0);
vector<int> respaldoHilos(34 * 5, 0);
//etiquetas de los registros que van a ser modificados, true = va a ser modificado
vector<bool> etiquetasRegistros(numRegistros, false);
IfId registroIfId = {0,{0,0,0,0}};
IdEx registroIdEx = {0,0,0,0,{0,0,0,0}};
ExMem registroExMem = {0,0,0,{0,0,0,0}};
MemWb registroMemWb = {0,0,{-0,0,0,0}};
Cache cacheDatos;
sem_t semIf,semId,semEx,semMem,semWb,semMain,semRegistros,semEspereId, semEspereEx,
      semEspereMem,semEspereWb;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;


void cargarInstrucciones(){
	int input;
	int i = 0;
	string c;
	cout<<"Escriba el nombre del archivo de instrucciones, respete mayusuculas"<<endl;
	bool salir;
    for(int j=0; j<1; j++){
        salir = false;
        while(!salir){
            getline(cin,c);
            const char *entrada = c.c_str();
            ifstream instrucciones(entrada);
            if(!instrucciones.is_open()){
                cerr<<"No se pudo abrir el archivo,intente de nuevo"<<endl;
            }
            else{
                while (instrucciones >> input && i < tamanoInstrucciones){
                    memoria[i] = input;
                    i++;
                }
                salir = true;
            }
        }
    }
    /**
    cout<<"Escriba el valor del quantum, debe ser positivo y diferente a 0"<<endl;
    salir = false;
    while(!salir){
    	getline(cin,c);
    	if(c == "0"){
    		cout<<"Escribio 0, Por favor escriba un valor diferente"<<endl;
    	}
    	else{
    	quantum = std::atoi(c.c_str());
    	cout<<"El tiempo quantum fue seteado en "<< quantum << " ciclos de reloj"<<endl;
    	salir = true;
    	}
    }
    **/
}

void inicializarSemaforos(){
    sem_init(&semIf,0,0);
    sem_init(&semId,0,0);
    sem_init(&semEx,0,0);
    sem_init(&semMem,0,0);
    sem_init(&semWb,0,0);
    sem_init(&semMain,0,0);
    sem_init(&semEspereId,0,0);
    sem_init(&semEspereEx,0,0);
    sem_init(&semEspereWb,0,0);
    sem_init(&semEspereMem,0,0);
    sem_init(&semRegistros,0,0);
}

//Un ciclo de reloj se simulara con hilos esperando cada uno un semaforo,
//una vez terminada su rutina.
void iniciarCicloReloj(){
	sem_post(&semIf);
	sem_post(&semId);
	sem_post(&semEx);
	sem_post(&semMem);
	sem_post(&semWb);
}

int loadCache(int numBloque){
    int indice = numBloque % 8;
    int indiceMemoria = numBloque * 4;
    if(cacheDatos.etiqueta[indice] != numBloque){
    	if(cacheDatos.estado[indice] == 'm'){
				///reemplazo bloque modificado, escribir en memoria
				int dirMemoriaViejo = cacheDatos.etiqueta[indice]*4;
				for(int i = 0;i<4;i++){
					memoria[dirMemoriaViejo+i] = cacheDatos.bloques[indice][i];
				}
				cout<<"Holis"<<endl;
        }
        cacheDatos.etiqueta[indice] = numBloque;
        for(int i=0; i<4; i++){
        	cacheDatos.bloques[indice][i] = memoria[indiceMemoria + i];
        }
            cacheDatos.estado[indice] = 'c';
    }
    return indice;
}


void *rutinaIF(void *args){
   bool suicidio = false;
   while(true){
       sem_wait(&semIf);
       sem_wait(&semEspereId);

       if(ingresarInstruccionesIF){
    	   ///hay un branch tomado
    	   cout<<"Soy if en el exmem cond hay un"<<registroExMem.cond<<endl;
    	   cout<<"Soy if en el registro Exmem ir hay un"<< registroExMem.ir[0]<<endl;
    	   if((registroExMem.ir[0] == 4 || registroExMem.ir[0] == 5)
    	      	     	      			   && (registroExMem.cond == 1)){
    		   cout<<"Soy if entre al branch tomado"<<endl;
    		   cout<<"PC actual "<<pc<<"nuevo pc "<<registroExMem.aluOutput<<endl;
     		   pc = registroExMem.aluOutput;
     		   //se pone en cero para que if no lea nuevamente un branch tomado en el siguiente
     		   //ciclo ya que ex quien es el que actualiza el exMem
     		   //esta sin instrucciones para procesar por el atraso del branch.
     		   registroExMem.ir[0] = 0;
       	   }
           //liberar id, notar que esto siempre lo hace IF mientras el tambien este activo
    	   //cargando instrucciones. Como id lee primero el registro ifid, no sucedera
    	   //el caso de que al liberar id, if cargue nuevas instrucciones antes de que
    	   //id haya podido leer.
    	   ingresarInstruccionesID = true;

    	   cout<<"Soy if cargando instruccion# "<<memoria[pc]<<endl;
           //cargar instruccion en IF/ID
    	   for(int i = 0; i < 4; i++){
    		   registroIfId.ir[i] = memoria[pc+i];
    	   }
    	   //se cargo un branch, detener instrucciones hasta que se resuelva el mismo.
    	   //esta variable la pone en true el main.
    	   if(registroIfId.ir[0] == 4 || registroIfId.ir[0] == 5){
    	   					ingresarInstruccionesIF = false;
    	   }

    	   else if(registroIfId.ir[0] == 63){
    		   suicidio = true;
    	   }

       	   pc += 4;
       	   registroIfId.npc = pc;

       }

       pthread_mutex_lock( &mutex1 );

       if(suicidio){
    	   hilosFinalizados++;
       }

       if(etapasFinalizadas < barrera){
    	   etapasFinalizadas++;
       }

       else{
    	   sem_post(&semMain);
       }

       pthread_mutex_unlock( &mutex1 );

       if(suicidio){
    	   pthread_exit(NULL);
       }
   }
   return 0;
}

void *rutinaID(void *args){
	bool suicidio = false;
    int aTemp = -1;
    int bTemp = -1;
    int immTemp = -1;
    	while(true){
    		sem_wait(&semId);
    		sem_wait(&semRegistros);//espera a que wb escriba en registros

    		if(ingresarInstruccionesID){
				switch(registroIfId.ir[0]){
					case 63:
						suicidio = true;
						break;

					//BEQZ
					case 4:
						//detener ingreso de instrucciones, no hay prediccion
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[1]]){
                    	   cout<<"Soy id, el registro"<<registroIfId.ir[1]<<" esta siendo bloqueando para escritura "<<endl;
                       }else{
                           conflictoDatos = true;
                       }
						break;
				   //BNEZ
				   case 5:
                       cout << "Etiqueta Registros " << etiquetasRegistros[registroIfId.ir[1]] << endl;
                       aTemp = registros[registroIfId.ir[1]];
                       bTemp = registros[registroIfId.ir[2]];
                       immTemp = registroIfId.ir[3];
                       if(!etiquetasRegistros[registroIfId.ir[1]]){
                    	   cout<<"Soy id, el registro"<<registroIfId.ir[1]<<" esta siendo bloqueando para escritura "<<endl;
                       }else{
                           conflictoDatos = true;
                       }
					   break;
					case 8:
						cout << "Etiqueta Registros " << etiquetasRegistros[registroIfId.ir[2]] << endl;
						aTemp = registros[registroIfId.ir[1]];
			            bTemp = registros[registroIfId.ir[2]];
	                    immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[2]]){
							cout<<"Soy id el registro "<< registroIfId.ir[2]<<" esta siendo bloqueado para escritura"<<endl;
		                    etiquetasRegistros[registroIfId.ir[2]] = true;
				        }
						else{
							cout << "Hay conflicto de datos con el registro "<< registroIfId.ir[2] << endl;
							conflictoDatos = true;
						}

						break;
				   case 32:
                        aTemp = registros[registroIfId.ir[1]];
                        bTemp = registros[registroIfId.ir[2]];
                        immTemp = registroIfId.ir[3];
                        if(!etiquetasRegistros[registroIfId.ir[3]]){
                            etiquetasRegistros[registroIfId.ir[3]] = true;
                        }else{
                            conflictoDatos = true;
                        }
						break;

					case 34:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[3]]){
                            etiquetasRegistros[registroIfId.ir[3]] = true;
                        }else{
                            conflictoDatos = true;
                        }
						break;
					case 12:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[3]]){
                            etiquetasRegistros[registroIfId.ir[3]] = true;
                        }else{
                            conflictoDatos = true;
                        }
						break;
					case  14:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[3]]){
                            etiquetasRegistros[registroIfId.ir[3]] = true;
                        }else{
                            conflictoDatos = true;
                        }
                        break;
					//LW
					case 35:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[2]]){
                            etiquetasRegistros[registroIfId.ir[2]] = true;
                        }else{
                            conflictoDatos = true;
                        }
						break;
					//SW
					case 43:
						aTemp = registros[registroIfId.ir[1]];
						immTemp = registroIfId.ir[3];
						bTemp = registros[registroIfId.ir[2]];
						break;
					//CASE 50, INSTRUC LL,MISMO PROCEDIMIENTO QUE LW LO DIFERENTE ES EN WB
					case 50:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						break;
					//CASE SC
					case 51:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
                        break;
				}
    		}

			sem_wait(&semEspereEx);

            if(ingresarInstruccionesID && !conflictoDatos){
            	registroIdEx.a = aTemp;
				registroIdEx.b = bTemp;
				registroIdEx.immm = immTemp;

                if(registroIdEx.ir[0] == 4 || registroIdEx.ir[0] == 5){
                    ingresarInstruccionesEx = true;
                }
				cout<<"Soy id pasando la instruccion "<< registroIfId.ir[0]<<" de ifid a idex"<<endl;

				for(int i = 0; i < 4; i++){
					registroIdEx.ir[i] = registroIfId.ir[i];
				}

				registroIdEx.npc = registroIfId.npc;
				//en caso de que id no estuviera leyendo el registro idex por branch
				//se deja leer el idex una vez id lo actualizo, por si se da el caso de branch
				//tomado.

				cout << "Estado del conflicto de datos " << conflictoDatos << endl;
            }
				//si lo que se leyo es un branch o hay un conflicto de datos, hay que detener id hasta
				//que este se resuelva. IF libera esta variable.
            if(registroIdEx.ir[0] == 4 || registroIdEx.ir[0] == 5){
					ingresarInstruccionesID = false;
					ingresarInstruccionesIF = false;
            }
            else if(conflictoDatos){
					ingresarInstruccionesID = false;
					ingresarInstruccionesIF = false;
					//registroIdEx.ir[0] = 0;
            }else if (registroIdEx.ir[0] != 4 || registroIdEx.ir[0] != 5){
                    ingresarInstruccionesIF = true;
            }



        sem_post(&semEspereId);

        pthread_mutex_lock( &mutex1 );

        if(suicidio){
        	hilosFinalizados++;
        }

        if(etapasFinalizadas < barrera){
        	etapasFinalizadas++;
        }
        else{
        	sem_post(&semMain);
        }
        pthread_mutex_unlock( &mutex1 );

        if(suicidio){
        	pthread_exit(NULL); //Muerase
      	}
    }

    return 0;
}

void *rutinaEX(void *args){
	  bool suicidio = false;
      int aluOutputTemp = -1;
      int bTemp = -1;
      int condTemp = 0;

      while(true){
    	    sem_wait(&semEx);
    	    if(ingresarInstruccionesEx){
                switch(registroIdEx.ir[0]){
                      case 63:
                          suicidio = true;
                          break;
                      case 32:
                          aluOutputTemp = registroIdEx.a + registroIdEx.b;
                          break;
                      case 8:
                          aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                          cout<<"Soy EX estoy sumando los valores "<< registroIdEx.a << " " << registroIdEx.immm<<endl;
                          break;
                      case 34:
                          aluOutputTemp = registroIdEx.a - registroIdEx.b;
                          break;
                      case 12:
                          aluOutputTemp = registroIdEx.a * registroIdEx.b;
                          break;
                      case  14:
                          aluOutputTemp = registroIdEx.a / registroIdEx.b;
                          break;
                      case 35:
                          aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                          break;
                      case 43:
                          aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                          bTemp = registroIdEx.b;
                          break;
                      case 50:
                          aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                          break;
                      case 51:
                          aluOutputTemp = registroIdEx.a + registroIdEx.immm;
                          bTemp = registroIdEx.b;
                          break;
                      //BEQZ
                      case 4:
                          if(registroIdEx.a == 0){
                              //computar nuevo pc
                              //if tiene acceso a EXMEM, libro pag 661,cambiar if
                              condTemp = 1; //simular que fue true la comparacion
                              aluOutputTemp = registroIdEx.npc + registroIdEx.immm;

                          }
                      break;
                      //BNQZ
                      case 5:
                          cout<<"el valor del registro del salto tiene un " << registroIdEx.a<<endl;
                          if(registroIdEx.a != 0){
                              cout<<"Soy ex entre case 5 branch BNQZ tomado"<<endl;
                              //computar nuevo pc
                              //setear pc en if, if tiene acceso a EXMEM,lbro pag 661
                              condTemp = 1; //simular que fue true la comparacion
                              aluOutputTemp = registroIdEx.npc + registroIdEx.immm;

                              cout<<"Soy ex hay un " << condTemp << " en condtemp y un " <<
                                      aluOutputTemp <<" en aluOutputTemp"<<endl;

                          }
                      break;
                }

    	    }
			sem_wait(&semEspereMem);

			//si leyo un branch se evita que actualice el registroExmem hasta que if lo lea.
			cout<<"Soy ex el candado ingresarInstruccionesEx tiene un "<<ingresarInstruccionesEx<<endl;

				registroExMem.aluOutput = aluOutputTemp;
				registroExMem.b = bTemp;
				registroExMem.cond = condTemp;

				cout<<"Soy ex pasando la instruccion "<< registroIdEx.ir[0]<<" de idex a exmem"<<endl;
				for(int i = 0; i < 4; i++){
					registroExMem.ir[i] = registroIdEx.ir[i];
				}

				if(registroExMem.ir[0] == 4 || registroExMem.ir[0] == 5){
					cout<<"Soy ex tengo un branch en exmem, me bloqueo"<<endl;
                    ingresarInstruccionesEx = false;
					//matamos la instruccion de branch que quedo.
                    registroIfId.ir[0] = 0;
				}



            sem_post(&semEspereEx);

			pthread_mutex_lock( &mutex1 );

			if(suicidio){
				 hilosFinalizados++;
			}

			if(etapasFinalizadas < barrera){
				 etapasFinalizadas++;
			}
			else{
				 sem_post(&semMain);
			}

			pthread_mutex_unlock( &mutex1 );

			if(suicidio){
				 pthread_exit(NULL); //Muerase
			}
    }
    return 0;
}


void *rutinaMEM(void *args){
    bool suicidio = false;
	while(true){
	    sem_wait(&semMem);
        sem_wait(&semEspereWb);

	    cout<<"Soy Mem pasando la instruccion "<< registroExMem.ir[0]<<" de Exmem a memWb"<<endl;
	    for(int i = 0; i < 4; i++){
        	registroMemWb.ir[i] = registroExMem.ir[i];
        }

        if(registroMemWb.ir[0] == 63){
        	suicidio = true;
        }

        //Actualizacion del ALUOutput
        registroMemWb.aluOutput = registroExMem.aluOutput;

        if(registroMemWb.ir[0] == 35 || registroMemWb.ir[0] == 50){
        	//calcular Bloque
        	int posEnCache = loadCache((tamanoInstrucciones + registroExMem.aluOutput)/4);
        	registroMemWb.lmd = cacheDatos.bloques[posEnCache][(tamanoInstrucciones + registroExMem.aluOutput)%4];
        }

        //operacion Store
        else if(registroMemWb.ir[0] == 43){
        	int posEnCache = loadCache((tamanoInstrucciones + registroMemWb.aluOutput)/4);
        	cacheDatos.estado[posEnCache] = 'm';
        	cacheDatos.bloques[posEnCache][(tamanoInstrucciones + registroMemWb.aluOutput)%4] = registroExMem.b;
        	memoria[768 + registroMemWb.aluOutput] = registroExMem.b;
        }

        //Operacion SC
        else if(registroMemWb.ir[0] == 51){
        	int posEnCache = loadCache((tamanoInstrucciones + registroMemWb.aluOutput)/4);
        	//caso que RL difiere de lo que cargo LL
        	if(cacheDatos.bloques[posEnCache][(tamanoInstrucciones + registroMemWb.aluOutput)%4]
        	   != registros[32]){
        		cout<<"Instruccion SC fallo"<<endl;
        		registroMemWb.lmd = 0;
        	}
        	else{
        		cout<<"Instruccion SC exitosa"<<endl;
        		registroMemWb.lmd  = 1;
        		cacheDatos.estado[posEnCache] = 'm';
        		cacheDatos.bloques[posEnCache][(tamanoInstrucciones + registroMemWb.aluOutput)%4] = registroExMem.b;
        	}
        }


        sem_post(&semEspereMem);

        pthread_mutex_lock( &mutex1 );
        if(suicidio){
        	hilosFinalizados++;
        }

	    if(etapasFinalizadas < barrera){
            etapasFinalizadas++;
	    }
        else{
        	cout<<"valorbarrera= "<<barrera<<"etapasF= "<<etapasFinalizadas<<endl;
	        sem_post(&semMain);
	    }

	    pthread_mutex_unlock( &mutex1 );

	    if(suicidio){
	    	pthread_exit(NULL); //Muerase
	    }
    }
	return 0;
}

void *rutinaWB(void *args){
	bool suicidio = false;
    while(true){
	    sem_wait(&semWb);
    	cout<<"Soy Wb tengo en MemWb la instruccion " << registroMemWb.ir[0]<<endl;
	    if(registroMemWb.ir[0] == 63){
	    	suicidio = true;
	    }

	    //Operaciones registro immm
	    else if(registroMemWb.ir[0] == 8){
            cout<<"Soy wb traigo en mi aluOutput un " << registroMemWb.aluOutput << endl;
	        registros[registroMemWb.ir[2]] = registroMemWb.aluOutput;
	        etiquetasRegistros[registroMemWb.ir[2]] = false;
	        ingresarInstruccionesID = true;
            conflictoDatos = false;

	    }
	    //Operaciones registro registro
	    else if(registroMemWb.ir[0] == 32 || registroMemWb.ir[0] == 34 || registroMemWb.ir[0] == 12 ||
	 	       registroMemWb.ir[0] == 14){

	    	registros[registroMemWb.ir[3]] = registroMemWb.aluOutput;
	    	etiquetasRegistros[registroMemWb.ir[2]] = false;
	        ingresarInstruccionesID = true;
            conflictoDatos = false;
	    }
	    //Operacion Load
	    else if(registroMemWb.ir[0] == 35){

	    	registros[registroMemWb.ir[2]] = registroMemWb.lmd;
	    }
	    //operacion LL
	    else if(registroMemWb.ir[0] == 50){

	    	registros[registroMemWb.ir[2]] = registroMemWb.lmd;
	    	//guardar en RL ergo ultimo registro de los 32
	    	registros[32] = registroMemWb.lmd;
	    }

	    else if(registroMemWb.ir[0] == 51){
	    	registros[registroMemWb.ir[2]] = registroMemWb.lmd;
	    }




	    sem_post(&semRegistros);
	    sem_post(&semEspereWb);

	    pthread_mutex_lock( &mutex1 );

	    if(suicidio){
	    	hilosFinalizados++;
	    }

	    if(etapasFinalizadas < barrera){
            etapasFinalizadas++;
	    }
	    else{
	        sem_post(&semMain);
	    }

	    pthread_mutex_unlock( &mutex1 );

	    if(suicidio){
	    	  pthread_exit(NULL);
	    }

    }
    return 0;
}

int main (){
	pthread_t threads[NUM_THREADS];
    bool finalizarEjecucion = false;

    //Inicializar Cache
     for(int i = 0; i < 8; i++ ){
    	for(int j = 0; j < 4 ; j++){
    		cacheDatos.bloques[i][j] = 0;
    	}
    	cacheDatos.estado[i] = 'c';
    	cacheDatos.etiqueta[i] = -1;
    }


    //cargar instrucciones
    cargarInstrucciones();

	//iniciar semaforos de cada etapa del pipeline, para controlar ciclos de reloj.
	inicializarSemaforos();

	//crearHilos
	pthread_create(&threads[0], NULL, rutinaIF,NULL);
	pthread_create(&threads[1], NULL, rutinaID,NULL);
	pthread_create(&threads[2], NULL, rutinaEX,NULL);
	pthread_create(&threads[3], NULL, rutinaMEM,NULL);
	pthread_create(&threads[4], NULL, rutinaWB,NULL);


	while(!finalizarEjecucion){
		ciclo++;
		cout<<"Se inicio el ciclo de reloj# " << ciclo << endl;

		//Si hay un 4 o 5 en ExMem.ir[0] = la instruccion de branch fue leida y
		//resuelta por ex, ergo se debe dejar pasar de nuevo las instrucciones.
		cout << "Instruccion EX/MEM " << registroExMem.ir[0] << endl;
		if(registroExMem.ir[0] == 4 || registroExMem.ir[0] == 5){
		    ingresarInstruccionesIF = true;
        }

		iniciarCicloReloj();

		sem_wait(&semMain);
        etapasFinalizadas = 0;
        barrera = 4 - hilosFinalizados;
        if(hilosFinalizados == 5 || ciclo > 20){
		   finalizarEjecucion = true;
        }
	}

    printf("Fin de la ejecución del programa:\n");
    printf("\nRegistros: \n");
    for(int i=0; i<33; i++){
        printf("\tR%d = %d\n", i, registros[i]);
    }
    /*printf("\nMemoria: \n");
    for(int i=768; i<1600; i++){
        printf("\tM%d = %d\n", i, memoria[i]);
    }*/

    printf("Fin de la ejecución del programa:\n");
    printf("\nEstado Registros: \n");
    for(int i=0; i<33; i++){
        printf("\tR%d = %d\n", i, etiquetasRegistros[i]);
    }

    cout<<"Cache:"<<endl;
    for(int i = 0; i < 8; i++ ){
    	for(int j = 0; j < 4 ; j++){
    		cout<<cacheDatos.bloques[i][j]<< " ";
    	}
    	cout<<endl;
    }



    printf("\nCiclos de reloj: %d\n", ciclo);
	exit(0);

}









