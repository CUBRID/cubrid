pipeline {
  agent none

  triggers {
    pollSCM('H 1 * * *')
  }

  stages {
    stage('Build and Test') {
      parallel {
        stage('Linux Release') {
          agent {
            docker {
              image 'cubridci/cubridci:develop'
              label 'linux'
              alwaysPull true
            }
          }
          environment {
            MAKEFLAGS = '-j'
            TEST_REPORT = 'reports'
          }
          steps {
            echo 'Checking out...'
            dir(path: 'cubridmanager') {
              git 'https://github.com/CUBRID/cubrid-manager-server'
            }
            
            echo 'Building...'
	    sh 'scl enable devtoolset-6 -- /entrypoint.sh build'

            echo 'Packing...'
            dir(path: 'packages') {
              deleteDir()
            }
	    sh 'scl enable devtoolset-6 -- /entrypoint.sh dist -o packages'

            echo 'Testing...'
            dir(path: 'reports') {
              deleteDir()
            }
            sh '/entrypoint.sh test || echo "$? failed"'
          }
          post {
            always {
              archiveArtifacts 'packages/*'
              junit 'reports/*.xml'
            }
          }
        }

        stage('Linux Debug') {
          agent {
            docker {
              image 'cubridci/cubridci:develop'
              label 'linux'
              alwaysPull true
            }
          }
          environment {
            MAKEFLAGS = '-j'
            TEST_REPORT = 'reports'
          }
          steps {
            echo 'Checking out...'
            dir(path: 'cubridmanager') {
              git 'https://github.com/CUBRID/cubrid-manager-server'
            }
            
            echo 'Building...'
	    sh 'scl enable devtoolset-6 -- /entrypoint.sh build -m debug'
            
            echo 'Packing...'
            dir(path: 'packages') {
              deleteDir()
            }
	    sh 'scl enable devtoolset-6 -- /entrypoint.sh dist -m debug -o packages'

            echo 'Testing...'
            dir(path: 'reports') {
              deleteDir()
            }
            sh '/entrypoint.sh test || echo "$? failed"'
          }
          post {
            always {
              archiveArtifacts 'packages/*'
              junit 'reports/*.xml'
            }
          }
        }

        stage('Windows Release') {
          agent {
            node {
              label 'windows'
            }
          }
          steps {
            echo 'Checking out...'
            dir(path: 'cubridmanager') {
              git 'https://github.com/CUBRID/cubrid-manager-server'
            }
            
            echo 'Building...'
            dir(path: 'packages') {
              deleteDir()
            }
	    bat 'win/build.bat /out packages'
          }
          post {
            always {
              archiveArtifacts 'packages/*'
            }
          }
        }
      }
    }
  }

  post {
    always {
      build job: "${DEPLOY_JOB}", parameters: [string(name: 'PROJECT_NAME', value: "$JOB_NAME")],
            propagate: false
      emailext replyTo: '$DEFAULT_REPLYTO', to: '$DEFAULT_RECIPIENTS',
	       subject: '$DEFAULT_SUBJECT', body: '''${JELLY_SCRIPT,template="html"}'''
    }
  }
}
