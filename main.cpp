/* Proyecto Programado Arquitectura de Computadoras
 * Grupo 1
 * Prof:Ileana Alpizar
 * Xiannie Rivas Hylton B05206
 * Daniel Rivera Solano A85274
 * Simulacion de procesador MIPS sin forwarding ni prediccion de branches
 *
 * Para la sincronizacion de hilos se utilizo la libreria pthreads en un sistema operativo linux(ubuntu)
 * Como requerimiento para correr el programa se debe agregar la sentencia -pthread a la hora de hacer
 * la compilacion del mismo.Si se utiliza un IDE como Eclipse tambien funciona agregar a las
 * opciones del linker del proyecto con click derecho al proyecto->properties->C/C++ Build->Settings
 * ->GCC C++ Linker->Libraries->escribir aqui "pthread".
 *
 * Aspectos que quedaron pendientes
 *
 * -No se logro terminar de implementar el cambio de contexto, esto por razon de que al comenzar a
 * implementarlo se descubrieron errores en manejo de conflictos de datos, los cuales se subestimo
 * el tiempo que se iba a tardar en solucionarlos.
 * .Por ende aunque el programa le pide el quantum al usuario este solo le permite ingresar un
 * hilo.
 * -No se implemento atrasos por fallo de Cache
 * -No se logro hacer correctamente la conversion de direcciones de memoria a indices de las estructuras
 * del programa, ya que los hilos de la profesora utilizan posiciones en memoria y el programa indexacion
 * con arreglos.
 *
 *
 * */



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
//el registro 32 representa el RL
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
//hilosUsuarioFinalizados = 0;
//para saber cuantos hilos que introdujo el usuario han finalizado ejecucion
//cambio de contexto
int pc = 0;
int etapasFinalizadas = 0;
int barrera = 4;
int hilosFinalizados = 0;
vector<int> memoria(tamanoMemoria, 1);
//registros inicializados en 1
vector<int> registros(numRegistros,0);
//respaldo de hilos para el cambio de contexto, se guarda los 32 registros + el RL + el PC
vector<int> respaldoHilos(34 * 5, 0);
//etiquetas para identificar registros que van a ser modificados, true = va a ser modificado
//para conflictos de datos
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
           //revisar si hay un branch tomado
    	   if((registroExMem.ir[0] == 4 || registroExMem.ir[0] == 5)
    	      	     	      			   && (registroExMem.cond == 1)){

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


           //cargar instruccion en IF/ID
    	   for(int i = 0; i < 4; i++){
    		   registroIfId.ir[i] = memoria[pc+i];
    	   }
    	   //se cargo un branch, detener instrucciones hasta que se resuelva el mismo.
    	   //esta variable la pone en true el main.
    	   if(registroIfId.ir[0] == 4 || registroIfId.ir[0] == 5){
                ingresarInstruccionesIF = false;
    	   }

    	   //cambiar por cambio de contexto, el hilo debe morir cuando lea instruccion 63
    	   //y sea el ultimo hilo que queda por ejecutar
    	   else if(registroIfId.ir[0] == 63){

    		   suicidio = true;
    	   }

       	   pc += 4;
       	   registroIfId.npc = pc;

       }

       pthread_mutex_lock( &mutex1 );
       //la logica del la sincronizacion
       //Se solicita un mutex para entrar a esta seccion critica, todas las etapas lo hacen
       //Una barrera es utilizada para que un hilo sepa si es el ultimo en terminar en un ciclo de reloj
       //la barrera comienza en 4 y es actualizada conforme se van matando los hilos de las etapas
       //y se actualiza hilos finalizados
       if(suicidio){
    	   hilosFinalizados++;
       }

       //si esto se cumple, no soy el ultimo hilo en terminar, asi que solo aumento contador
       if(etapasFinalizadas < barrera){
    	   etapasFinalizadas++;
       }
       //si lo anterior no se cumple, si soy el utlimo hilo en terminar, asi que debo levantar el main
       //que se encuentra detas de un semaforo para que inicie nuevo ciclo de reloj
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
    //temporales para que id lea de if/id, ya que debe esperar que ex lea de id/ex
    //para que haya mejor paralelismo
    	while(true){
    		sem_wait(&semId);
    		sem_wait(&semRegistros);//espera a que wb escriba en registros

    		if(ingresarInstruccionesID){
				switch(registroIfId.ir[0]){
					case 63:
						suicidio = true;
						break;

				//se chequea la etiqueta de los registros operandos, si alguna es true como la condicion
			    //esta negada no entrara al if y creara el retraso necesario con la variable
				//conflicto Datos

                    //JR
					case 2:
                        if(!etiquetasRegistros[registroIfId.ir[1]]){
                            ingresarInstruccionesIF = false;
                            pc = pc + registros[registroIfId.ir[1]];
                        }else{
                            conflictoDatos = true;
                        }
						break;

                    //JAL
                    case 3:
                        //registroIfId.ir[0] = -1;
                        registros[32] = pc;
                        pc = pc + registroIfId.ir[3];
						ingresarInstruccionesIF = false;
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
                       aTemp = registros[registroIfId.ir[1]];
                       bTemp = registros[registroIfId.ir[2]];
                       immTemp = registroIfId.ir[3];
                       if(!etiquetasRegistros[registroIfId.ir[1]]){
                    	   cout<<"Soy id, el registro"<<registroIfId.ir[1]<<" esta siendo bloqueando para escritura "<<endl;
                       }else{
                           conflictoDatos = true;
                       }
					   break;

                    //DADDI
					case 8:
						aTemp = registros[registroIfId.ir[1]];
			            bTemp = registros[registroIfId.ir[2]];
	                    immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[1]]){
							cout<<"Soy id el registro "<< registroIfId.ir[2]<<" esta siendo bloqueado para escritura"<<endl;
		                    etiquetasRegistros[registroIfId.ir[2]] = true;
				        }
						else{
							conflictoDatos = true;
						}

						break;

                    //DADD
                    case 32:
                        aTemp = registros[registroIfId.ir[1]];
                        bTemp = registros[registroIfId.ir[2]];
                        immTemp = registroIfId.ir[3];
                        if(!etiquetasRegistros[registroIfId.ir[1]] && !etiquetasRegistros[registroIfId.ir[2]]){
                            cout<<"Soy id el registro "<< registroIfId.ir[3]<<" esta siendo bloqueado para escritura"<<endl;
                            etiquetasRegistros[registroIfId.ir[3]] = true;
                        }else{

                            conflictoDatos = true;
                        }
						break;

                    //DSUB
					case 34:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[1]] && !etiquetasRegistros[registroIfId.ir[2]]){
                            etiquetasRegistros[registroIfId.ir[3]] = true;
                        }else{
                            conflictoDatos = true;
                        }
						break;

                    //DMUL
					case 12:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[2];
						if(!etiquetasRegistros[registroIfId.ir[1]] && !etiquetasRegistros[registroIfId.ir[2]]){
                            etiquetasRegistros[registroIfId.ir[3]] = true;
                        }else{
                            conflictoDatos = true;
                        }
						break;

                    //DDIV
					case  14:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[1]] && !etiquetasRegistros[registroIfId.ir[2]]){
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
						if(!etiquetasRegistros[registroIfId.ir[1]]){
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
						if(etiquetasRegistros[registroIfId.ir[1]] || etiquetasRegistros[registroIfId.ir[2]]){
                            conflictoDatos = true;
                        }

						break;

					//CASE 50, INSTRUC LL
					case 50:
						aTemp = registros[registroIfId.ir[1]];
						bTemp = registros[registroIfId.ir[2]];
						immTemp = registroIfId.ir[3];
						if(!etiquetasRegistros[registroIfId.ir[1]]){
                            etiquetasRegistros[registroIfId.ir[2]] = true;
                        }else{
                            conflictoDatos = true;
                        }
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
            //escribir en id/ex si no hay conflicto de datos ni retraso
            if(ingresarInstruccionesID && !conflictoDatos){
            	registroIdEx.a = aTemp;
				registroIdEx.b = bTemp;
				registroIdEx.immm = immTemp;

				//SI id logra escribir en id/ex, debe siempre liberar a ex, por los casos de retrasos
				// que ex se encuentren tambien no escribiendo en registro intermedio por retraso
                if(registroIdEx.ir[0] == 4 || registroIdEx.ir[0] == 5){
                    ingresarInstruccionesEx = true;
                }


				for(int i = 0; i < 4; i++){
					registroIdEx.ir[i] = registroIfId.ir[i];
				}

                if(!ingresarInstruccionesEx)
                    ingresarInstruccionesEx = true;

				registroIdEx.npc = registroIfId.npc;
				//en caso de que id no estuviera leyendo el registro idex por branch
				//se deja leer el idex una vez id lo actualizo, por si se da el caso de branch
				//tomado.


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
                              //computar nuevo pc, comparacion exitosa
                              //if tiene acceso a EXMEM
                              condTemp = 1; //simular que fue true la comparacion
                              aluOutputTemp = registroIdEx.npc + registroIdEx.immm;

                     }
                     break;
                      //BNQZ
                      case 5:
                          if(registroIdEx.a != 0){

                              //computar nuevo pc
                              //setear pc en if, if tiene acceso a EXMEM,lbro pag 661
                              condTemp = 1; //simular que fue true la comparacion
                              aluOutputTemp = registroIdEx.npc + registroIdEx.immm;

                      }
                      break;
                }

    	    }
			sem_wait(&semEspereMem);




				registroExMem.aluOutput = aluOutputTemp;
				registroExMem.b = bTemp;
				registroExMem.cond = condTemp;


				for(int i = 0; i < 4; i++){
					registroExMem.ir[i] = registroIdEx.ir[i];
				}

				if(registroExMem.ir[0] == 4 || registroExMem.ir[0] == 5){
					cout<<"Soy ex tengo un branch en exmem, me bloqueo"<<endl;
                    ingresarInstruccionesEx = false;
					//matamos la instruccion de branch que quedo.
                    registroIfId.ir[0] = 0;
				}
                if(conflictoDatos){
                    ingresarInstruccionesEx = false;
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
        	if(conflictoDatos){
                ingresarInstruccionesID = true;
        	}
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
        		//Instruccion SC fallo
        		registroMemWb.lmd = 0;
        	}
        	else{
        		//Instruccion SC exitosa"
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

	    if(registroMemWb.ir[0] == 63){
	    	suicidio = true;
	    }

	    //Operaciones registro immm
	    else if(registroMemWb.ir[0] == 8){

	        registros[registroMemWb.ir[2]] = registroMemWb.aluOutput;
	        //wb pone etiquetas de modificacion en registros = false ya que ya modifico el registro en
	        //cuestion
	        etiquetasRegistros[registroMemWb.ir[2]] = false;
	        //libera id en caso de conflicto de datos
	        ingresarInstruccionesID = true;
	        //con esto id puede revisar de nuevo si no los operandos de la instruccion no tienen
	        //conflictos
            conflictoDatos = false;

	    }
	    //Operaciones registro registro
	    else if(registroMemWb.ir[0] == 32 || registroMemWb.ir[0] == 34 || registroMemWb.ir[0] == 12 ||
	 	       registroMemWb.ir[0] == 14){

	    	registros[registroMemWb.ir[3]] = registroMemWb.aluOutput;
	    	etiquetasRegistros[registroMemWb.ir[3]] = false;
	        ingresarInstruccionesID = true;
            conflictoDatos = false;
	    }
	    //Operacion Load
	    else if(registroMemWb.ir[0] == 35){

	    	registros[registroMemWb.ir[2]] = registroMemWb.lmd;
	    	etiquetasRegistros[registroMemWb.ir[2]] = false;
	    	ingresarInstruccionesID = true;
            conflictoDatos = false;
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
    	//estado default compartidos
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
	    if(registroExMem.ir[0] == 4 || registroExMem.ir[0] == 5){
		    ingresarInstruccionesIF = true;
        }
        //mismo caso para JAL y JR
        if(registroIdEx.ir[0] == 2 || registroIdEx.ir[0] == 3){
            ingresarInstruccionesIF = true;
        }

		iniciarCicloReloj();

		sem_wait(&semMain);
        etapasFinalizadas = 0;
        //se actualiza la barrera de acuerdo a el numero de hilos(etapas que finalizaron ejecucion)
        barrera = 4 - hilosFinalizados;
        //si los 5 hilos finalizaron ejecucion, se dejan de iniciar nuevos ciclos de reloj
        if(hilosFinalizados == 5){
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









