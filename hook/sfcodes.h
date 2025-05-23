// For documentation please see: https://xrpl-hooks.readme.io/reference/
// Generated using generate_sfcodes.sh
#define sfCloseResolution ((16U << 16U) + 1U)
#define sfMethod ((16U << 16U) + 2U)
#define sfTransactionResult ((16U << 16U) + 3U)
#define sfTickSize ((16U << 16U) + 16U)
#define sfUNLModifyDisabling ((16U << 16U) + 17U)
#define sfHookResult ((16U << 16U) + 18U)
#define sfLedgerEntryType ((1U << 16U) + 1U)
#define sfTransactionType ((1U << 16U) + 2U)
#define sfSignerWeight ((1U << 16U) + 3U)
#define sfTransferFee ((1U << 16U) + 4U)
#define sfVersion ((1U << 16U) + 16U)
#define sfHookStateChangeCount ((1U << 16U) + 17U)
#define sfHookEmitCount ((1U << 16U) + 18U)
#define sfHookExecutionIndex ((1U << 16U) + 19U)
#define sfHookApiVersion ((1U << 16U) + 20U)
#define sfNetworkID ((2U << 16U) + 1U)
#define sfFlags ((2U << 16U) + 2U)
#define sfSourceTag ((2U << 16U) + 3U)
#define sfSequence ((2U << 16U) + 4U)
#define sfPreviousTxnLgrSeq ((2U << 16U) + 5U)
#define sfLedgerSequence ((2U << 16U) + 6U)
#define sfCloseTime ((2U << 16U) + 7U)
#define sfParentCloseTime ((2U << 16U) + 8U)
#define sfSigningTime ((2U << 16U) + 9U)
#define sfExpiration ((2U << 16U) + 10U)
#define sfTransferRate ((2U << 16U) + 11U)
#define sfWalletSize ((2U << 16U) + 12U)
#define sfOwnerCount ((2U << 16U) + 13U)
#define sfDestinationTag ((2U << 16U) + 14U)
#define sfHighQualityIn ((2U << 16U) + 16U)
#define sfHighQualityOut ((2U << 16U) + 17U)
#define sfLowQualityIn ((2U << 16U) + 18U)
#define sfLowQualityOut ((2U << 16U) + 19U)
#define sfQualityIn ((2U << 16U) + 20U)
#define sfQualityOut ((2U << 16U) + 21U)
#define sfStampEscrow ((2U << 16U) + 22U)
#define sfBondAmount ((2U << 16U) + 23U)
#define sfLoadFee ((2U << 16U) + 24U)
#define sfOfferSequence ((2U << 16U) + 25U)
#define sfFirstLedgerSequence ((2U << 16U) + 26U)
#define sfLastLedgerSequence ((2U << 16U) + 27U)
#define sfTransactionIndex ((2U << 16U) + 28U)
#define sfOperationLimit ((2U << 16U) + 29U)
#define sfReferenceFeeUnits ((2U << 16U) + 30U)
#define sfReserveBase ((2U << 16U) + 31U)
#define sfReserveIncrement ((2U << 16U) + 32U)
#define sfSetFlag ((2U << 16U) + 33U)
#define sfClearFlag ((2U << 16U) + 34U)
#define sfSignerQuorum ((2U << 16U) + 35U)
#define sfCancelAfter ((2U << 16U) + 36U)
#define sfFinishAfter ((2U << 16U) + 37U)
#define sfSignerListID ((2U << 16U) + 38U)
#define sfSettleDelay ((2U << 16U) + 39U)
#define sfTicketCount ((2U << 16U) + 40U)
#define sfTicketSequence ((2U << 16U) + 41U)
#define sfNFTokenTaxon ((2U << 16U) + 42U)
#define sfMintedNFTokens ((2U << 16U) + 43U)
#define sfBurnedNFTokens ((2U << 16U) + 44U)
#define sfHookStateCount ((2U << 16U) + 45U)
#define sfEmitGeneration ((2U << 16U) + 46U)
#define sfLockCount ((2U << 16U) + 49U)
#define sfFirstNFTokenSequence ((2U << 16U) + 50U)
#define sfXahauActivationLgrSeq ((2U << 16U) + 96U)
#define sfImportSequence ((2U << 16U) + 97U)
#define sfRewardTime ((2U << 16U) + 98U)
#define sfRewardLgrFirst ((2U << 16U) + 99U)
#define sfRewardLgrLast ((2U << 16U) + 100U)
#define sfIndexNext ((3U << 16U) + 1U)
#define sfIndexPrevious ((3U << 16U) + 2U)
#define sfBookNode ((3U << 16U) + 3U)
#define sfOwnerNode ((3U << 16U) + 4U)
#define sfBaseFee ((3U << 16U) + 5U)
#define sfExchangeRate ((3U << 16U) + 6U)
#define sfLowNode ((3U << 16U) + 7U)
#define sfHighNode ((3U << 16U) + 8U)
#define sfDestinationNode ((3U << 16U) + 9U)
#define sfCookie ((3U << 16U) + 10U)
#define sfServerVersion ((3U << 16U) + 11U)
#define sfNFTokenOfferNode ((3U << 16U) + 12U)
#define sfEmitBurden ((3U << 16U) + 13U)
#define sfHookInstructionCount ((3U << 16U) + 17U)
#define sfHookReturnCode ((3U << 16U) + 18U)
#define sfReferenceCount ((3U << 16U) + 19U)
#define sfTouchCount ((3U << 16U) + 97U)
#define sfAccountIndex ((3U << 16U) + 98U)
#define sfAccountCount ((3U << 16U) + 99U)
#define sfRewardAccumulator ((3U << 16U) + 100U)
#define sfEmailHash ((4U << 16U) + 1U)
#define sfTakerPaysCurrency ((17U << 16U) + 1U)
#define sfTakerPaysIssuer ((17U << 16U) + 2U)
#define sfTakerGetsCurrency ((17U << 16U) + 3U)
#define sfTakerGetsIssuer ((17U << 16U) + 4U)
#define sfLedgerHash ((5U << 16U) + 1U)
#define sfParentHash ((5U << 16U) + 2U)
#define sfTransactionHash ((5U << 16U) + 3U)
#define sfAccountHash ((5U << 16U) + 4U)
#define sfPreviousTxnID ((5U << 16U) + 5U)
#define sfLedgerIndex ((5U << 16U) + 6U)
#define sfWalletLocator ((5U << 16U) + 7U)
#define sfRootIndex ((5U << 16U) + 8U)
#define sfAccountTxnID ((5U << 16U) + 9U)
#define sfNFTokenID ((5U << 16U) + 10U)
#define sfEmitParentTxnID ((5U << 16U) + 11U)
#define sfEmitNonce ((5U << 16U) + 12U)
#define sfEmitHookHash ((5U << 16U) + 13U)
#define sfBookDirectory ((5U << 16U) + 16U)
#define sfInvoiceID ((5U << 16U) + 17U)
#define sfNickname ((5U << 16U) + 18U)
#define sfAmendment ((5U << 16U) + 19U)
#define sfHookOn ((5U << 16U) + 20U)
#define sfDigest ((5U << 16U) + 21U)
#define sfChannel ((5U << 16U) + 22U)
#define sfConsensusHash ((5U << 16U) + 23U)
#define sfCheckID ((5U << 16U) + 24U)
#define sfValidatedHash ((5U << 16U) + 25U)
#define sfPreviousPageMin ((5U << 16U) + 26U)
#define sfNextPageMin ((5U << 16U) + 27U)
#define sfNFTokenBuyOffer ((5U << 16U) + 28U)
#define sfNFTokenSellOffer ((5U << 16U) + 29U)
#define sfHookStateKey ((5U << 16U) + 30U)
#define sfHookHash ((5U << 16U) + 31U)
#define sfHookNamespace ((5U << 16U) + 32U)
#define sfHookSetTxnID ((5U << 16U) + 33U)
#define sfOfferID ((5U << 16U) + 34U)
#define sfEscrowID ((5U << 16U) + 35U)
#define sfURITokenID ((5U << 16U) + 36U)
#define sfGovernanceFlags ((5U << 16U) + 99U)
#define sfGovernanceMarks ((5U << 16U) + 98U)
#define sfEmittedTxnID ((5U << 16U) + 97U)
#define sfAmount ((6U << 16U) + 1U)
#define sfBalance ((6U << 16U) + 2U)
#define sfLimitAmount ((6U << 16U) + 3U)
#define sfTakerPays ((6U << 16U) + 4U)
#define sfTakerGets ((6U << 16U) + 5U)
#define sfLowLimit ((6U << 16U) + 6U)
#define sfHighLimit ((6U << 16U) + 7U)
#define sfFee ((6U << 16U) + 8U)
#define sfSendMax ((6U << 16U) + 9U)
#define sfDeliverMin ((6U << 16U) + 10U)
#define sfMinimumOffer ((6U << 16U) + 16U)
#define sfRippleEscrow ((6U << 16U) + 17U)
#define sfDeliveredAmount ((6U << 16U) + 18U)
#define sfNFTokenBrokerFee ((6U << 16U) + 19U)
#define sfHookCallbackFee ((6U << 16U) + 20U)
#define sfLockedBalance ((6U << 16U) + 21U)
#define sfBaseFeeDrops ((6U << 16U) + 22U)
#define sfReserveBaseDrops ((6U << 16U) + 23U)
#define sfReserveIncrementDrops ((6U << 16U) + 24U)
#define sfPublicKey ((7U << 16U) + 1U)
#define sfMessageKey ((7U << 16U) + 2U)
#define sfSigningPubKey ((7U << 16U) + 3U)
#define sfTxnSignature ((7U << 16U) + 4U)
#define sfURI ((7U << 16U) + 5U)
#define sfSignature ((7U << 16U) + 6U)
#define sfDomain ((7U << 16U) + 7U)
#define sfFundCode ((7U << 16U) + 8U)
#define sfRemoveCode ((7U << 16U) + 9U)
#define sfExpireCode ((7U << 16U) + 10U)
#define sfCreateCode ((7U << 16U) + 11U)
#define sfMemoType ((7U << 16U) + 12U)
#define sfMemoData ((7U << 16U) + 13U)
#define sfMemoFormat ((7U << 16U) + 14U)
#define sfFulfillment ((7U << 16U) + 16U)
#define sfCondition ((7U << 16U) + 17U)
#define sfMasterSignature ((7U << 16U) + 18U)
#define sfUNLModifyValidator ((7U << 16U) + 19U)
#define sfValidatorToDisable ((7U << 16U) + 20U)
#define sfValidatorToReEnable ((7U << 16U) + 21U)
#define sfHookStateData ((7U << 16U) + 22U)
#define sfHookReturnString ((7U << 16U) + 23U)
#define sfHookParameterName ((7U << 16U) + 24U)
#define sfHookParameterValue ((7U << 16U) + 25U)
#define sfBlob ((7U << 16U) + 26U)
#define sfAccount ((8U << 16U) + 1U)
#define sfOwner ((8U << 16U) + 2U)
#define sfDestination ((8U << 16U) + 3U)
#define sfIssuer ((8U << 16U) + 4U)
#define sfAuthorize ((8U << 16U) + 5U)
#define sfUnauthorize ((8U << 16U) + 6U)
#define sfRegularKey ((8U << 16U) + 8U)
#define sfNFTokenMinter ((8U << 16U) + 9U)
#define sfEmitCallback ((8U << 16U) + 10U)
#define sfHookAccount ((8U << 16U) + 16U)
#define sfInform ((8U << 16U) + 99U)
#define sfIndexes ((19U << 16U) + 1U)
#define sfHashes ((19U << 16U) + 2U)
#define sfAmendments ((19U << 16U) + 3U)
#define sfNFTokenOffers ((19U << 16U) + 4U)
#define sfHookNamespaces ((19U << 16U) + 5U)
#define sfURITokenIDs ((19U << 16U) + 99U)
#define sfPaths ((18U << 16U) + 1U)
#define sfTransactionMetaData ((14U << 16U) + 2U)
#define sfCreatedNode ((14U << 16U) + 3U)
#define sfDeletedNode ((14U << 16U) + 4U)
#define sfModifiedNode ((14U << 16U) + 5U)
#define sfPreviousFields ((14U << 16U) + 6U)
#define sfFinalFields ((14U << 16U) + 7U)
#define sfNewFields ((14U << 16U) + 8U)
#define sfTemplateEntry ((14U << 16U) + 9U)
#define sfMemo ((14U << 16U) + 10U)
#define sfSignerEntry ((14U << 16U) + 11U)
#define sfNFToken ((14U << 16U) + 12U)
#define sfEmitDetails ((14U << 16U) + 13U)
#define sfHook ((14U << 16U) + 14U)
#define sfSigner ((14U << 16U) + 16U)
#define sfMajority ((14U << 16U) + 18U)
#define sfDisabledValidator ((14U << 16U) + 19U)
#define sfEmittedTxn ((14U << 16U) + 20U)
#define sfHookExecution ((14U << 16U) + 21U)
#define sfHookDefinition ((14U << 16U) + 22U)
#define sfHookParameter ((14U << 16U) + 23U)
#define sfHookGrant ((14U << 16U) + 24U)
#define sfGenesisMint ((14U << 16U) + 96U)
#define sfActiveValidator ((14U << 16U) + 95U)
#define sfImportVLKey ((14U << 16U) + 94U)
#define sfHookEmission ((14U << 16U) + 93U)
#define sfMintURIToken ((14U << 16U) + 92U)
#define sfAmountEntry ((14U << 16U) + 91U)
#define sfSigners ((15U << 16U) + 3U)
#define sfSignerEntries ((15U << 16U) + 4U)
#define sfTemplate ((15U << 16U) + 5U)
#define sfNecessary ((15U << 16U) + 6U)
#define sfSufficient ((15U << 16U) + 7U)
#define sfAffectedNodes ((15U << 16U) + 8U)
#define sfMemos ((15U << 16U) + 9U)
#define sfNFTokens ((15U << 16U) + 10U)
#define sfHooks ((15U << 16U) + 11U)
#define sfMajorities ((15U << 16U) + 16U)
#define sfDisabledValidators ((15U << 16U) + 17U)
#define sfHookExecutions ((15U << 16U) + 18U)
#define sfHookParameters ((15U << 16U) + 19U)
#define sfHookGrants ((15U << 16U) + 20U)
#define sfGenesisMints ((15U << 16U) + 96U)
#define sfActiveValidators ((15U << 16U) + 95U)
#define sfImportVLKeys ((15U << 16U) + 94U)
#define sfHookEmissions ((15U << 16U) + 93U)
#define sfAmounts ((15U << 16U) + 92U)